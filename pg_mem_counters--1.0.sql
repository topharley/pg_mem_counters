-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_mem_counters" to load this file. \quit

CREATE FUNCTION inc_mem_counter(counter text, increment int8 default 0) RETURNS int8
	AS 'MODULE_PATHNAME'
	LANGUAGE C VOLATILE;
	
CREATE FUNCTION get_mem_counter_rpm(counter text) RETURNS int8
	AS 'MODULE_PATHNAME'
	LANGUAGE C VOLATILE;	

CREATE FUNCTION mem_counters() RETURNS TABLE (
	name text,
	total int8,
	rpm int8
)
AS 'MODULE_PATHNAME' LANGUAGE c STRICT;

