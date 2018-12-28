#!/usr/bin/python
# babeltrace_zipkin.py

import sys
sys.path.append("../../babeltrace-plugins")
import json
import getopt
from babeltrace import *
from scribe_client import ScribeClient

HELP = "Usage: python babeltrace_zipkin.py path/to/file -s <server> -p <port>"
CATEGORY = "LTTng"


def main(argv):
    try:
        path = argv[0]
    except:
        raise TypeError(HELP)

    try:
        opts, args = getopt.getopt(argv[1:], "hs:p:")
    except getopt.GetoptError:
        raise TypeError(HELP)

    server = None
    port = None
    for opt, arg in opts:
        if opt == '-h':
            raise TypeError(HELP)
        elif opt == '-s':
            server = arg
        elif opt == '-p':
            port = arg

    if not server:
        server = "localhost"
    if not port:
        port = 1463

    # Open connection with scribe
    scribe_client = ScribeClient(port,  server)

    # Create TraceCollection and add trace:
    traces = TraceCollection()
    trace_handle = traces.add_trace(path, "ctf")
    if trace_handle is None:
        raise IOError("Error adding trace")

    #iterate over events
    for event in traces.events:
        data = dict()

        data["parent_span_id"]= event["parent_span_id"]
        data['name'] = event["trace_name"]
        data ["trace_id"] = event["trace_id"]
        data["span_id"] = event["span_id"]
	data['port'] = event['port_no']
	data['service_name'] = event['service_name']
	data['ip'] = event['ip']
	data['evemt'] = event['event']
	data['timestamp'] = event.timestamp
	'''
        for k, v in event.items():
            field_type = event._field(k).type
            data[k] = format_value(field_type, v)
	'''
        json_data = json.dumps(data)

        #send data to scribe
        scribe_client.log(CATEGORY, json_data)

    scribe_client.close()


def format_value(field_type, value):

    if field_type == 1:
        return int(value)
    elif field_type == 2:
        return float(value)
    elif field_type == 8:
        return [x for x in value]
    else:
        return str(value)

if __name__ == "__main__":
    main(sys.argv[1:])
