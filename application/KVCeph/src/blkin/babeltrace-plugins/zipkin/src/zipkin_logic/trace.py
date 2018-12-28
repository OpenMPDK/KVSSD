import math
import time
import random


class Trace(object):
    """
    An L{ITrace} provider which delegates to zero or more L{ITracers} and
    allows setting a default L{IEndpoint} to associate with L{IAnnotation}s

    @ivar _tracers: C{list} of one or more L{ITracer} providers.
    @ivar _endpoint: An L{IEndpoint} provider.
    """
    def __init__(self, name, trace_id=None, span_id=None,
                 parent_span_id=None, tracers=None):
        """
        @param name: C{str} describing the current span.
        @param trace_id: C{int} or C{None}
        @param span_id: C{int} or C{None}
        @param parent_span_id: C{int} or C{None}

        @param tracers: C{list} of L{ITracer} providers, primarily useful
            for unit testing.
        """
        self.name = name
        # If no trace_id and span_id are given we want to generate new
        # 64-bit integer ids.
        self.trace_id = trace_id
        self.span_id = span_id

        # If no parent_span_id is given then we assume there is no parent span
        # and leave it as None.
        self.parent_span_id = parent_span_id

        # If no tracers are given we get the global list of tracers.
        self._tracers = tracers

        # By default no endpoint will be associated with annotations recorded
        # to this trace.
        self._endpoint = None

    def __ne__(self, other):
        return not self == other

    def __repr__(self):
        return (
            '{0.__class__.__name__}({0.name!r}, trace_id={0.trace_id!r}, '
            'span_id={0.span_id!r}, parent_span_id={0.parent_span_id!r})'
        ).format(self)

    def set_endpoint(self, endpoint):
        """
        Set a default L{IEndpoint} provider for the current L{Trace}.
        All annotations recorded after this endpoint is set will use it,
        unless they provide their own endpoint.
        """
        self._endpoint = endpoint


class Endpoint(object):

    def __init__(self, ipv4, port, service_name):
        """
        @param ipv4: C{str} ipv4 address.
        @param port: C{int} port number.
        @param service_name: C{str} service name.
        """
        self.ipv4 = ipv4
        self.port = port
        self.service_name = service_name

    def __ne__(self, other):
        return not self == other

    def __repr__(self):
        return ('{0.__class__.__name__}({0.ipv4!r}, {0.port!r}, '
                '{0.service_name!r})').format(self)


class Annotation(object):

    def __init__(self, name, value, annotation_type, endpoint=None):
        """
        @param name: C{str} name of this annotation.

        @param value: A value of the appropriate type based on
            C{annotation_type}.

        @param annotation_type: C{str} the expected type of our C{value}.

        @param endpoint: An optional L{IEndpoint} provider to associate with
            this annotation or C{None}
        """
        self.name = name
        self.value = value
        self.annotation_type = annotation_type
        self.endpoint = endpoint

    def __ne__(self, other):
        return not self == other

    def __repr__(self):
        return (
            '{0.__class__.__name__}({0.name!r}, {0.value!r}, '
            '{0.annotation_type!r}, {0.endpoint})'
        ).format(self)

    @classmethod
    def timestamp(cls, name, timestamp=None):
        if timestamp is None:
            timestamp = math.trunc(time.time() * 1000 * 1000)

        return cls(name, timestamp, 'timestamp')

    @classmethod
    def client_send(cls, timestamp=None):
        return cls.timestamp(constants.CLIENT_SEND, timestamp)

    @classmethod
    def client_recv(cls, timestamp=None):
        return cls.timestamp(constants.CLIENT_RECV, timestamp)

    @classmethod
    def server_send(cls, timestamp=None):
        return cls.timestamp(constants.SERVER_SEND, timestamp)

    @classmethod
    def server_recv(cls, timestamp=None):
        return cls.timestamp(constants.SERVER_RECV, timestamp)

    @classmethod
    def string(cls, name, value):
        return cls(name, value, 'string')

    @classmethod
    def bytes(cls, name, value):
        return cls(name, value, 'bytes')
