/*
 * Zipkin lttng-ust tracepoint provider.
 */

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER zipkin

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./zipkin_trace.h"

#if !defined(_ZIPKIN_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _ZIPKIN_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(
        zipkin,
        keyval,
        TP_ARGS(char *, service, char *, trace_name,
            int, port, char *, ip, long, trace,
            long, span, long, parent_span,
            char *, key, char *, val ),

        TP_FIELDS(
                ctf_string(trace_name, trace_name)
                ctf_string(service_name, service)
                ctf_integer(int, port_no, port)
                ctf_string(ip, ip)
                ctf_integer(long, trace_id, trace)
        ctf_integer(long, span_id, span)
                ctf_integer(long, parent_span_id, parent_span)
        ctf_string(key, key)
        ctf_string(val, val)
        )
)
TRACEPOINT_LOGLEVEL(
        zipkin,
        keyval,
        TRACE_WARNING)


TRACEPOINT_EVENT(
        zipkin,
        timestamp,
        TP_ARGS(char *, service, char *, trace_name,
            int, port, char *, ip, long, trace,
            long, span, long, parent_span,
            char *, event),

        TP_FIELDS(
                ctf_string(trace_name, trace_name)
                ctf_string(service_name, service)
                ctf_integer(int, port_no, port)
                ctf_string(ip, ip)
                ctf_integer(long, trace_id, trace)
        ctf_integer(long, span_id, span)
                ctf_integer(long, parent_span_id, parent_span)
                ctf_string(event, event)
        )
)
TRACEPOINT_LOGLEVEL(
        zipkin,
        timestamp,
        TRACE_WARNING)
#endif /* _ZIPKIN_H */

#include <lttng/tracepoint-event.h>
