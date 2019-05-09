#include "postgres.h"
#include "access/hash.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/spin.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/hsearch.h"
#include "utils/guc.h"
#include "utils/syscache.h"
#include "utils/lsyscache.h"
#include "utils/timestamp.h"
#include "utils/backend_random.h"
#include "storage/lwlock.h"
#include "storage/lmgr.h"
#include "storage/shmem.h"
#include "pgstat.h"
#include "storage/fd.h"
#include <time.h>
#include <sys/time.h>

#define MAXCOUNTERSNAMELEN	50

PG_MODULE_MAGIC;

typedef struct pgmcCounterData
{
	slock_t			mutex;	/* protects the fields below */
	int64			total;	/* the current value of the counter */

	int32			currentMinute;
	int32			currentSecond;
	int32			secFrequencies[60];
	int32			msecFrequencies[1000];
} pgmcCounterData;

/*
* Global shared state
*/
typedef struct pgmcSharedState
{
	/* protects the hashtable */
	LWLockId		lock;
} pgmcSharedState;

/*
* The key for a hash table entry in shared memory.
*/
typedef struct pgmcSharedHashKey
{
	const char *name_ptr;
	int			name_len;
} pgmcSharedHashKey;

/*
* Hash table entry for a counter in shared memory.  The hash table only
* contains an offset into the "counters" array in the shared state structure.
*/
typedef struct pgmcSharedEntry
{
	pgmcSharedHashKey	key;			/* hash key of entry - MUST BE FIRST */
	pgmcCounterData		counter_data;	/* counter data */
	char				counter_name[1];	/* VARIABLE LENGTH ARRAY - MUST BE LAST */
} pgmcSharedEntry;


extern Datum inc_mem_counter(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(inc_mem_counter);
extern Datum get_mem_counter(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(get_mem_counter);
extern Datum mem_counters(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(mem_counters);


extern void _PG_init(void);
extern void _PG_fini(void);

static pgmcCounterData *pgmc_find_counter(pgmcSharedHashKey *key);
static pgmcCounterData *pgmc_upsert_counter(const char *name);
static int32 pgmc_get_counter_rpm(volatile pgmcCounterData *counter, int64 increment);
static void pgmc_shmem_startup(void);
static uint32 pgmc_shared_hash_fn(const void *key, Size keysize);
static int pgmc_shared_match_fn(const void *key1, const void *key2, Size keysize);
static Size pgmc_memsize(void);


static int _pgmc_max = 1024;
static shmem_startup_hook_type _prev_shmem_startup_hook = NULL;

static pgmcSharedState *_pgmc = NULL;
static HTAB *_pgmc_shared_hash = NULL;


Datum
inc_mem_counter(PG_FUNCTION_ARGS)
{
	text *counter_name = PG_GETARG_TEXT_PP(0);
	int64 increment = PG_GETARG_INT64(1);
	volatile pgmcCounterData *counter;
	int64 newvalue;

	if (VARSIZE(counter_name) - VARHDRSZ > MAXCOUNTERSNAMELEN)
	{
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("The name of the counter must be no longer than 50 bytes in length")));
	}

	if (!_pgmc)
	{
		elog(ERROR, "get_mem_counter: _pgmc is NULL! did you remember to add 'pg_mem_counters' to the shared_preload_libraries?");
	}

	counter = pgmc_upsert_counter(text_to_cstring(counter_name));
	if (!counter)
	{
		/* couldn't whatever or whatever */
		PG_RETURN_NULL();
	}

	SpinLockAcquire(&counter->mutex);
	pgmc_get_counter_rpm(counter, increment);
	newvalue = counter->total;
	SpinLockRelease(&counter->mutex);

	PG_RETURN_INT64(newvalue);
}

Datum
get_mem_counter(PG_FUNCTION_ARGS)
{
	text *counter_name = PG_GETARG_TEXT_PP(0);
	volatile pgmcCounterData *counter;
	int64 rpm;

	if (VARSIZE(counter_name) - VARHDRSZ > MAXCOUNTERSNAMELEN)
	{
		ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("The name of the counter must be no longer than 50 bytes in length")));
	}

	if (!_pgmc)
	{
		elog(ERROR, "get_mem_counter: _pgmc is NULL! did you remember to add 'pg_mem_counters' to the shared_preload_libraries?");
	}

	counter = pgmc_upsert_counter(text_to_cstring(counter_name));
	if (!counter)
	{
		/* couldn't whatever or whatever */
		PG_RETURN_INT64(0);
	}

	SpinLockAcquire(&counter->mutex);
	rpm = pgmc_get_counter_rpm(counter, 0);
	SpinLockRelease(&counter->mutex);

	PG_RETURN_INT64(rpm);
}

extern Datum
mem_counters(PG_FUNCTION_ARGS)
{
	int					i;
	int					num_counters;
	HASH_SEQ_STATUS		hash_seq;
	pgmcSharedEntry    *entry;
	pgmcCounterData	  **counters;
	char			  **counter_names;
	ReturnSetInfo	   *rsinfo = (ReturnSetInfo *)fcinfo->resultinfo;
	TupleDesc			tupdesc;
	Tuplestorestate	   *tupstore;
	MemoryContext		oldcxt;

	if (!_pgmc)
	{
		elog(ERROR, "get_mem_counter: _pgmc is NULL! did you remember to add 'pg_mem_counters' to the shared_preload_libraries?");
	}

	/* switch to long-lived memory context */
	oldcxt = MemoryContextSwitchTo(rsinfo->econtext->ecxt_per_query_memory);

	tupstore = tuplestore_begin_heap(rsinfo->allowedModes & SFRM_Materialize_Random,
		false, work_mem);

	/* get the requested return tuple description */
	tupdesc = CreateTupleDescCopy(rsinfo->expectedDesc);
	if (tupdesc->natts != 3)
		elog(ERROR, "unexpected natts %d", tupdesc->natts);

	LWLockAcquire(_pgmc->lock, LW_SHARED);

	num_counters = hash_get_num_entries(_pgmc_shared_hash);
	counters = (pgmcCounterData **)palloc(sizeof(pgmcCounterData *) * num_counters);
	counter_names = (char **)palloc(sizeof(char *) * num_counters);

	hash_seq_init(&hash_seq, _pgmc_shared_hash);
	i = 0;
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		Assert(num_counters > i);
		counters[i] = &entry->counter_data;
		counter_names[i] = entry->counter_name;
		++i;
	}
	Assert(num_counters == i);

	LWLockRelease(_pgmc->lock);

	for (i = 0; i < num_counters; ++i)
	{
		volatile pgmcCounterData *counter;
		int64 total;
		int32 rpm;
		Datum values[3];
		bool isnull[3] = { false, false, false };

		counter = counters[i];
		SpinLockAcquire(&counter->mutex);
		rpm = pgmc_get_counter_rpm(counter, 0);
		total = counter->total;
		SpinLockRelease(&counter->mutex);

		values[0] = CStringGetTextDatum(counter_names[i]);
		values[1] = Int64GetDatum(total);
		values[2] = Int64GetDatum(rpm);
		tuplestore_putvalues(tupstore, tupdesc, values, isnull);
	}

	MemoryContextSwitchTo(oldcxt);

	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	return (Datum)0;
}

static uint32 
sumFrequencies(volatile int32 *array, int count)
{
	int i;
	int32 result = 0;
	for (i = 0; i < count; i++)
		result += *(array + i);
	return result;
}

static int32
pgmc_get_counter_rpm(volatile pgmcCounterData *counter, int64 increment)
{
	int32			result = 0;
	pg_time_t	    stamp_time;
	struct timeval	stamp_timeval;
	struct pg_tm	pgm;
	int				nowMillisecond;
	int				nowSecond;
	int				nowMinute;
	int				i;
	
	//get time vars
	gettimeofday(&stamp_timeval, NULL);
	stamp_time = (pg_time_t)stamp_timeval.tv_sec;
	pgm = *pg_localtime(&stamp_time, log_timezone);
	nowSecond = pgm.tm_sec;
	nowMinute = pgm.tm_min;
	nowMillisecond = stamp_timeval.tv_usec / 1000;

	//save hits
	if (increment > 0)
	{
		counter->total += increment;
		counter->msecFrequencies[nowMillisecond] += increment;
	}

	//more then 1 minute w/o hits?
	if (nowMinute - counter->currentMinute > 1)
		for (i = 0; i < 60; i++)
			counter->secFrequencies[i] = 0;

	//check if we inside prev minute
	if (nowSecond != counter->currentSecond)
	{
		if (nowSecond < counter->currentSecond)
		{
			//clear old data: over minute
			for (i = counter->currentSecond + 1; i < 60; i++)
				counter->secFrequencies[i] = 0;
			for (i = 0; i < nowSecond; i++)
				counter->secFrequencies[i] = 0;
		}
		else
		{
			//clear old data:not over minute
			for (i = counter->currentSecond + 1; i < nowSecond; i++)
				counter->secFrequencies[i] = 0;
		}

		//write calculated 
		counter->secFrequencies[nowSecond] = sumFrequencies(counter->msecFrequencies, 1000);

		//reset second array
		for (i = 0; i < 1000; i++) counter->msecFrequencies[i] = 0;

		//save current 
		counter->currentSecond = nowSecond;
		counter->currentMinute = nowMinute;
	}

	//result is sum of minute array
	result = sumFrequencies(counter->secFrequencies, 60);

	return result;
}

static pgmcCounterData *
pgmc_find_counter(pgmcSharedHashKey *key)
{
	pgmcSharedEntry   *entry;

	Assert(_pgmc != NULL);

	/* Lookup the hash table entry with shared lock. */
	LWLockAcquire(_pgmc->lock, LW_SHARED);

	entry = (pgmcSharedEntry *)hash_search(_pgmc_shared_hash, key, HASH_FIND, NULL);
	LWLockRelease(_pgmc->lock);
	if (entry)
		return &entry->counter_data;
	else
		return NULL;
}

static pgmcCounterData *
pgmc_upsert_counter(const char *name)
{
	pgmcSharedHashKey	key;
	pgmcSharedEntry    *entry;
	bool				found;
	pgmcCounterData	   *counter;

	Assert(_pgmc != NULL);

	key.name_ptr = name;
	key.name_len = strlen(name);

	counter = pgmc_find_counter(&key);
	if (counter != NULL)
	{
		return counter;
	}

	/* Must acquire exclusive lock to add a new entry. */
	LWLockAcquire(_pgmc->lock, LW_EXCLUSIVE);
	if (hash_get_num_entries(_pgmc_shared_hash) >= _pgmc_max)
	{
		LWLockRelease(_pgmc->lock);
		return NULL;
	}

	entry = (pgmcSharedEntry *)hash_search(_pgmc_shared_hash, &key, HASH_ENTER, &found);
	if (found)
	{
		/* Entry found, initialize it */

		LWLockRelease(_pgmc->lock);
		return &entry->counter_data;
	}
	else
	{
		/* New entry, initialize it */

		/* dynhash tried to copy the key for us, but must fix name_ptr */
		entry->key.name_ptr = entry->counter_name;
		/* reset the statistics */
		memset(&entry->counter_data, 0, sizeof(pgmcCounterData));
		SpinLockInit(&entry->counter_data.mutex);
		entry->counter_data.currentMinute = 61;
		memcpy(entry->counter_name, key.name_ptr, key.name_len);
		entry->counter_name[key.name_len] = '\0';

		LWLockRelease(_pgmc->lock);

		return &entry->counter_data;
	}
}

void
_PG_init(void)
{
	/*
	* In order to create our shared memory area, we have to be loaded via
	* shared_preload_libraries.  If not, fall out without hooking into any of
	* the main system.
	*/
	if (!process_shared_preload_libraries_in_progress)
		return;

	RequestAddinShmemSpace(pgmc_memsize());
	RequestNamedLWLockTranche("pg_mem_counters", 1);

	_prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pgmc_shmem_startup;
}

void
_PG_fini(void)
{
	shmem_startup_hook = _prev_shmem_startup_hook;
}

static void
pgmc_shmem_startup(void)
{
	bool		found;
	HASHCTL		info;

	if (_prev_shmem_startup_hook)
		_prev_shmem_startup_hook();

	/* reset in case this is a restart within the postmaster */
	_pgmc = NULL;
	_pgmc_shared_hash = NULL;

	/*
	* Create or attach to the shared memory state, including hash table
	*/
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	_pgmc = ShmemInitStruct("pg_mem_counters",
		sizeof(pgmcSharedState),
		&found);

	if (!found)
	{
		/* First time through ... */
		_pgmc->lock = &(GetNamedLWLockTranche("pg_mem_counters"))->lock;
	}

	memset(&info, 0, sizeof(info));
	info.keysize = MAXCOUNTERSNAMELEN + 1;
	info.entrysize = offsetof(pgmcSharedEntry, counter_name) + MAXCOUNTERSNAMELEN + 1;
	info.hash = pgmc_shared_hash_fn;
	info.match = pgmc_shared_match_fn;
	_pgmc_shared_hash = ShmemInitHash("pg_mem_counters hash",
		_pgmc_max, _pgmc_max,
		&info,
		HASH_ELEM | HASH_FUNCTION | HASH_COMPARE);

	LWLockRelease(AddinShmemInitLock);
}

/*
* Calculate hash value for a key
*/
static uint32
pgmc_shared_hash_fn(const void *key, Size keysize)
{
	const pgmcSharedHashKey *k = (const pgmcSharedHashKey *)key;

	/* we don't bother to include encoding in the hash */
	return DatumGetUInt32(hash_any((const unsigned char *)k->name_ptr,
		k->name_len));
}

/*
* Compare two keys - zero means match
*/
static int
pgmc_shared_match_fn(const void *key1, const void *key2, Size keysize)
{
	const pgmcSharedHashKey *k1 = (const pgmcSharedHashKey *)key1;
	const pgmcSharedHashKey *k2 = (const pgmcSharedHashKey *)key2;

	if (k1->name_len == k2->name_len &&
		memcmp(k1->name_ptr, k2->name_ptr, k1->name_len) == 0)
		return 0;
	else
		return 1;
}

/*
* Estimate shared memory space needed.
*/
static Size
pgmc_memsize(void)
{
	Size		size;
	Size		entrysize;

	size = MAXALIGN(sizeof(pgmcSharedState));
	entrysize = offsetof(pgmcSharedEntry, counter_name) + MAXCOUNTERSNAMELEN + 1;
	size = add_size(size, hash_estimate_size(_pgmc_max, entrysize));

	return size;
}
