# Copyright 2012 Rackspace Hosting, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import struct
import socket

from thrift.protocol import TBinaryProtocol
from thrift.transport import TTransport

import ttypes


def hex_str(n):
    return '%0.16x' % (n,)


def json_formatter(traces, *json_args, **json_kwargs):
    json_traces = []

    for (trace, annotations) in traces:
        json_trace = {
            'trace_id': hex_str(trace.trace_id),
            'span_id': hex_str(trace.span_id),
            'name': trace.name,
            'annotations': []
        }

        if trace.parent_span_id:
            json_trace['parent_span_id'] = hex_str(trace.parent_span_id)

        for annotation in annotations:
            json_annotation = {
                'key': annotation.name,
                'value': annotation.value,
                'type': annotation.annotation_type
            }

            if annotation.endpoint:
                json_annotation['host'] = {
                    'ipv4': annotation.endpoint.ipv4,
                    'port': annotation.endpoint.port,
                    'service_name': annotation.endpoint.service_name
                }

            json_trace['annotations'].append(json_annotation)

        json_traces.append(json_trace)

    return json.dumps(json_traces, *json_args, **json_kwargs)


def ipv4_to_int(ipv4):
    return struct.unpack('!i', socket.inet_aton(ipv4))[0]


def base64_thrift(thrift_obj):
    trans = TTransport.TMemoryBuffer()
    tbp = TBinaryProtocol.TBinaryProtocol(trans)

    thrift_obj.write(tbp)
    res = trans.getvalue().encode('base64').strip()
    res = res.replace("\n","")
    #print res
    #print len(res)
    return res
    #return trans.getvalue().encode('base64').strip()


def binary_annotation_formatter(annotation, host=None):
    annotation_types = {
        'string': ttypes.AnnotationType.STRING,
        'bytes': ttypes.AnnotationType.BYTES,
    }

    annotation_type = annotation_types[annotation.annotation_type]

    value = annotation.value

    if isinstance(value, unicode):
        value = value.encode('utf-8')

    return ttypes.BinaryAnnotation(
        annotation.name,
        value,
        annotation_type,
        host)


def base64_thrift_formatter(trace, annotations):
    thrift_annotations = []
    binary_annotations = []

    for annotation in annotations:
        host = None
        if annotation.endpoint:
            host = ttypes.Endpoint(
                ipv4=ipv4_to_int(annotation.endpoint.ipv4),
                port=annotation.endpoint.port,
                service_name=annotation.endpoint.service_name)

        if annotation.annotation_type == 'timestamp':
            thrift_annotations.append(ttypes.Annotation(
                timestamp=annotation.value,
                value=annotation.name,
                host=host))
        else:
            binary_annotations.append(
                binary_annotation_formatter(annotation, host))

    thrift_trace = ttypes.Span(
        trace_id=trace.trace_id,
        name=trace.name,
        id=trace.span_id,
        parent_id=trace.parent_span_id,
        annotations=thrift_annotations,
        binary_annotations=binary_annotations
    )

    return base64_thrift(thrift_trace)
