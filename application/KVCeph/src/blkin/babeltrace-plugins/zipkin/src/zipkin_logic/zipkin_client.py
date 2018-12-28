#!/usr/bin/python

from scribe_client import ScribeClient
from trace import Annotation, Trace, Endpoint
from collections import defaultdict
from formatters import base64_thrift_formatter


class ZipkinClient(ScribeClient):

    DEFAULT_END_ANNOTATIONS = ("ss", "cr", "end")

    def __init__(self, port, host):
        super(ZipkinClient, self).__init__(port, host)
        self._annotations_for_trace = defaultdict(list)

    def create_trace(self, event):
        service = event["trace_name"]
        trace_id = event["trace_id"]
        span_id = event["span_id"]
        parent_span = event["parent_span_id"]
        if parent_span == 0:
            parent_span = None
        trace = Trace(service, trace_id, span_id, parent_span)
        return trace

    def create_annotation(self, event, kind):
        if kind == "keyval_string":
            key = event["key"]
            val = event["val"]
            annotation = Annotation.string(key, val)
        elif kind == "keyval_integer":
            key = event["key"]
            val = str(event["val"])
            annotation = Annotation.string(key, val)
        elif kind == "timestamp":
            timestamp = event.timestamp
            #timestamp has different digit length
            timestamp = str(timestamp)
            timestamp = timestamp[:-3]
            event_name = event["event"]
            annotation = Annotation.timestamp(event_name, int(timestamp))

        #  create and set endpoint
        port = event["port_no"]
        service = event["service_name"]
        ip = event["ip"]
        endpoint = Endpoint(ip, int(port), service)
        annotation.endpoint = endpoint

        print annotation
        return annotation

    def record(self, trace, annotation):
        self.scribe_log(trace, [annotation])
	'''
	trace_key = (trace.trace_id, trace.span_id)
        self._annotations_for_trace[trace_key].append(annotation)
        if (annotation.name in self.DEFAULT_END_ANNOTATIONS):
            saved_annotations = self._annotations_for_trace[trace_key]
            del self._annotations_for_trace[trace_key]
            self.scribe_log(trace, saved_annotations)
        print "Record event"
	'''

    def scribe_log(self, trace, annotations):
        trace._endpoint = None
        message = base64_thrift_formatter(trace, annotations)
        category = 'zipkin'
        return self.log(category, message)
