# -*- coding: utf-8 -*-

"""
Stderr built-in backend.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2017, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import out


PUBLIC = True


def generate_h_begin(events, group):
    out('#include "qemu/log-for-trace.h"',
        '#include "qemu/error-report.h"',
        '')


def generate_h(event, group):
    argnames = ", ".join(event.args.names())
    if len(event.args) > 0:
        argnames = ", " + argnames

    if "vcpu" in event.properties:
        # already checked on the generic format code
        cond = "true"
    else:
        # Not sure how this pipeline works, but these trace events only appear
        # if they've been declared in the source
        # e.g. cond -> trace_event_get_state(TRACE_UPSAT_COMM_MACHINE_INIT)
        cond = "trace_event_get_state(%s)" % ("TRACE_" + event.name.upper())

    # Looking to see if tracing is enabled, and if so, compile in
    # Akin to saying "if trace function exists" AND "tracing was enabled"
    # Dynamically change state, but make sure we react here
    # get_state is prob looking at internal memory, but for now replace with some external call
    trace_env = "TRACE_" + event.name.upper()
    out(
        '    if ( getenv("%s") ) {' % (trace_env), 
        '        if (message_with_timestamp) {',
        '            struct timeval _now;',
        '            gettimeofday(&_now, NULL);',
        '#line %(event_lineno)d "%(event_filename)s"',
        '            qemu_log("%%d@%%zu.%%06zu:%(name)s " %(fmt)s "\\n",',
        '                     qemu_get_thread_id(),',
        '                     (size_t)_now.tv_sec, (size_t)_now.tv_usec',
        '                     %(argnames)s);',
        '#line %(out_next_lineno)d "%(out_filename)s"',
        '        } else {',
        '#line %(event_lineno)d "%(event_filename)s"',
        '            qemu_log("%(name)s " %(fmt)s "\\n"%(argnames)s);',
        '#line %(out_next_lineno)d "%(out_filename)s"',
        '        }',
        '    }',
        cond=cond,
        event_lineno=event.lineno,
        event_filename=event.filename,
        name=event.name,
        fmt=event.fmt.rstrip("\n"),
        argnames=argnames)


def generate_h_backend_dstate(event, group):
    out('    trace_event_get_state_dynamic_by_id(%(event_id)s) || \\',
        event_id="TRACE_" + event.name.upper())
