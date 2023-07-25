#!@python@
import argparse
import datetime
import logging
import uuid

import gi
try:
    # Import required modules from the g-i repository
    gi.require_version('Ygg', '0.1')
    gi.require_version('GLib', '2.0')

    from gi.repository import Ygg, GLib
except ImportError or ValueError as e:
    logging.error("cannot import {}".format(e))


def transmit_done(worker, result):
    '''
        A callback that is invoked once when the `transmit` method call
        finishes.
    '''
    success, response_code, response_metadata, response_data = worker.transmit_finish(
        result)
    if success:
        logging.debug("response_code = {}".format(response_code))
        logging.debug("response_metadata = {}".format(response_metadata))
        logging.debug("response_data = {}".format(response_data.get_data()))

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
    logging.debug("data = {}".format(data.get_data()))

    # Emit the worker event "WORKING". This may optionally be used to signal the
    # dispatcher that the worker is actively working.
    worker.emit_event(Ygg.WorkerEvent.WORKING,
                      "working on data: {}".format(data))

    # Call the transmit function, sending `data` back to the dispatcher.
    worker.transmit(addr, str(uuid.uuid4()), id,
                    meta_data, data, None, transmit_done)


def handle_event(event):
    '''
        A callback that is invoked each time the worker receives an event signal
        from the dispatcher.
    '''
    print(event)


if __name__ == "__main__":
    # Set up argument parsing and a flag for log level
    parser = argparse.ArgumentParser()
    parser.add_argument("-l", "--log-level", help="set logging level")
    args = parser.parse_args()

    # Set the log level parsed from flags
    logging.basicConfig(level=args.log_level)

    # Create a worker with the directive value 'echo_py'.
    worker = Ygg.Worker(directive="echo_py",
                        remote_content=False, features=None)

    # Set a data receive handler function
    worker.set_rx_func(handle_rx)

    # Set an event receive handler function
    worker.set_event_func(handle_event)

    # Connect the worker
    worker.connect()

    # Start the main run loop
    loop = GLib.MainLoop()
    loop.run()
