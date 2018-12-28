#blkin

BlkKin is a project that enables you to trace low-overhead applications using
LTTng following the tracing semantics that are described in Google's [Dapper
Paper](http://static.googleusercontent.com/media/research.google.com/el//pubs/archive/36356.pdf)

According to this paper the logged information is called `Annotation` and
belongs to a specific span and trace. Each trace is comprised of multiple spans
which are related with each other with causal relationships. So, the BlkKin
library gives the user the API to easily instrument C/C++ applications. In
order to instrument applications you should take a look at ``blkin-lib/tests``
for some testcases and at the  ``blkin-lib/zipkin_c.h`` file

As a tracing backend BlkKin uses LTTng. So you must have LTTng installed.

In order to build and install the lib, go to blkin-lib folder and:

```
make
make install
```

You should take a look at the examples to find out how to link the blkin lib
with your instrumented application.

In order to visualize the aggregated information you can use Twitter's
[Zipkin](http://twitter.github.io/zipkin/) and send the data that you created,
by running the equivalent babeltrace plugin. In order to do you can run

```
./zipkin/src/babeltrace_zipkin.py </path/to/lttng/traces> -s <server_ip>
-p <port_number>

```

within the babeltrace-plugins directory.

In case you have not used the blkin-lib to instrument your application, you can
still send your data to a Scribe server. To do that you can use another
Babeltrace plugin. This plugin tranforms LTTng trace data to a JSON format and
sends them to a Scribe sever.To do so we can equivalently run

```
./json/src/babeltrace_json.py </path/to/lttng/traces> -s <server_ip>
-p <port_number>
```
within the babeltrace-plugins directory 

Both of these plugins require that you have installed Babeltrace with its
Python bindings enabled.
The path to the lttng traces should not be the root directory but the directory
where the channel directories are included.

## Dependencies

* libboost-all-dev 
* lttng-tools

Note that BlkKin is tested only with LTTng2.4
