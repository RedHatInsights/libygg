#!@python@
import datetime
import logging
import uuid

import gi
try:
    gi.require_version('Ygg', '0.1')
    gi.require_version('GLib', '2.0')

    from gi.repository import Ygg, GLib
except ImportError or ValueError as e:
    logging.error("cannot import {}".format(e))


def transmit_done(worker, result):
    success, response_code, response_metadata, response_data = worker.transmit_finish(
        result)
    if success:
        print("response_code = {}".format(response_code))
        print("response_metadata = {}".format(response_metadata))
        print("response_data = {}".format(response_data))

        worker.set_feature("UpdatedAt", datetime.datetime.now().isoformat())


def handle_rx(worker, addr, id, response_to, meta_data, data):
    '''
        A callback that is invoked each time the worker receives data from the
        dispatcher.
    '''
    print("addr = {}".format(addr))
    print("id = {}".format(id))
    print("response_to = {}".format(response_to))
    print("meta_data = {}".format(meta_data))
    print("data = {}".format(data))

    worker.emit_event(Ygg.WorkerEvent.WORKING,
                      "working on data: {}".format(data))

    worker.transmit(addr, str(uuid.uuid4()), id,
                    meta_data, data, None, transmit_done)


def handle_event(event):
    print(event)


if __name__ == "__main__":
    worker = Ygg.Worker(directive="echo", remote_content=False, features=None)
    worker.set_rx_func(handle_rx)
    worker.set_event_func(handle_event)
    worker.connect()

    loop = GLib.MainLoop()
    loop.run()
