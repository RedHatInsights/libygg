#!@python@
import argparse
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
        logging.debug("response_code = {}".format(response_code))
        logging.debug("response_metadata = {}".format(response_metadata))
        logging.debug("response_data = {}".format(response_data))

        worker.set_feature("UpdatedAt", datetime.datetime.now().isoformat())


def handle_rx(worker, addr, id, response_to, meta_data, data):
    '''
        A callback that is invoked each time the worker receives data from the
        dispatcher.
    '''
    logging.debug("handle_rx")
    logging.debug("addr = {}".format(addr))
    logging.debug("id = {}".format(id))
    logging.debug("response_to = {}".format(response_to))
    logging.debug("meta_data = {}".format(meta_data))
    logging.debug("data = {}".format(data))

    worker.emit_event(Ygg.WorkerEvent.WORKING,
                      "working on data: {}".format(data))

    worker.transmit(addr, str(uuid.uuid4()), id,
                    meta_data, data, None, transmit_done)


def handle_event(event):
    print(event)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-l", "--log-level", help="set logging level")
    args = parser.parse_args()
    logging.basicConfig(level=args.log_level)
    worker = Ygg.Worker(directive="echo_py",
                        remote_content=False, features=None)
    worker.set_rx_func(handle_rx)
    worker.set_event_func(handle_event)
    worker.connect()

    loop = GLib.MainLoop()
    loop.run()
