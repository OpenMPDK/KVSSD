#!/usr/bin/python
# scribe_client.py

from scribe import scribe
from thrift.transport import TTransport, TSocket
from thrift.protocol import TBinaryProtocol

class ScribeClient(object):

    def __init__(self, port, host):
        print host
        self.port = port
        self.host = host
        self.openConnection()

    def openConnection(self):
        socket = TSocket.TSocket(host=self.host, port=self.port)
        self.transport = TTransport.TFramedTransport(socket)
        protocol = TBinaryProtocol.TBinaryProtocol(trans=self.transport,
                                                   strictRead=False,
                                                   strictWrite=False)
        self.client = scribe.Client(protocol)
        self.transport.open()

    def log(self, category, message):
        log_entry = scribe.LogEntry(category, message)
        result = self.client.Log(messages=[log_entry])
        return result  # 0 for success

    def close(self):
        self.transport.close()
