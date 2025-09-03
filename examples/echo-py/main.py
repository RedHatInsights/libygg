#!@python@
import argparse
import datetime
import logging
import uuid

import gi

try:
    # Import required modules from the g-i repository
    gi.require_version("Ygg", "0")
    gi.require_version("Gio", "2.0")
    gi.require_version("GLib", "2.0")

    from gi.repository import Gio, GLib, Ygg
except ImportError or ValueError as e:
    logging.error("cannot import {}".format(e))

# Number of seconds to sleep before echoing a response.
sleep_delay = 0

# Number of iterations to repeat when echoing a response.
loop_times = 1

# A dictionary of ID-to-Gio.Cancellables to keep in case the message needs to be
# cancelled.
active_sources = {}


def transmit_done(worker, result):
    """
    A callback that is invoked once when the `transmit` method call
    finishes.
    """
    success, response_code, response_metadata, response_data = worker.transmit_finish(
        result
    )
    if success:
        logging.debug("response_code = {}".format(response_code))
        logging.debug("response_metadata = {}".format(response_metadata))
        logging.debug("response_data = {}".format(response_data.get_data()))

        worker.set_feature("UpdatedAt", datetime.datetime.now().isoformat())


def tx_cb(data):
    global active_sources

    if data["cancellable"].is_cancelled():
        logging.info("message {} is cancelled".format(data["id"]))
        return GLib.SOURCE_REMOVE

    logging.debug("loop iteration {} of {}".format(data["loops"], loop_times))
    worker.transmit(
        data["addr"],
        str(uuid.uuid4()),
        data["id"],
        data["metadata"],
        data["data"],
        None,
        transmit_done,
    )
    if data["loops"] < loop_times:
        data["loops"] += 1
        return GLib.SOURCE_CONTINUE
    else:
        data["loops"] = 1
        del active_sources[data["id"]]
        return GLib.SOURCE_REMOVE


def handle_rx(worker, addr, id, response_to, meta_data, data):
    """
    A callback that is invoked each time the worker receives data from the
    dispatcher.
    """
    logging.debug("handle_rx")
    logging.debug("addr = {}".format(addr))
    logging.debug("id = {}".format(id))
    logging.debug("response_to = {}".format(response_to))
    logging.debug("meta_data = {}".format(meta_data))
    logging.debug("data = {}".format(data.get_data()))

    event_data = Ygg.Metadata()
    event_data.set("message", data.get_data().decode(encoding="UTF-8"))

    # Emit the worker event "WORKING". This may optionally be used to signal the
    # dispatcher that the worker is actively working.
    worker.emit_event(Ygg.WorkerEvent.WORKING, id, response_to, event_data)

    cancellable = Gio.Cancellable.new()
    active_sources[id] = cancellable
    tx_data = {
        "addr": addr,
        "id": id,
        "response_to": response_to,
        "metadata": meta_data,
        "data": data,
        "cancellable": cancellable,
        "loops": 1,
    }
    GLib.timeout_add_seconds(sleep_delay, tx_cb, tx_data)


def handle_cancel(worker, addr, id, cancel_id):
    """
    A callback that is invoked when the worker receives a cancel message from
    the dispatcher.
    """
    global active_sources

    logging.debug("handle_cancel")
    cancellable = active_sources[cancel_id]
    if cancellable is not None:
        cancellable.cancel()
        del active_sources[cancel_id]


def handle_event(event):
    """
    A callback that is invoked each time the worker receives an event signal
    from the dispatcher.
    """
    print(event)


def disconnected_signal_handler(worker, loop):
    """
    A callback that is invoked when the worker emits the 'disconnected' signal.
    """
    logging.debug("disconnected_signal_handler")
    loop.quit()


if __name__ == "__main__":
    # Set up argument parsing and a flag for log level
    parser = argparse.ArgumentParser()
    parser.add_argument("-l", "--log-level", help="set logging level")
    parser.add_argument(
        "-d", "--directive", help="connect using directive", default="echo_py"
    )
    parser.add_argument(
        "-L", "--loop", help="Number of times to repeat the echo", default=1, type=int
    )
    parser.add_argument(
        "-s",
        "--sleep",
        help="Sleep time in seconds before echoing the response",
        default=0,
        type=int,
    )
    args = parser.parse_args()

    # Set loop_times value parsed from flags
    loop_times = args.loop

    # Set sleep_delay value parsed from flags
    sleep_delay = args.sleep

    # Set the log level parsed from flags
    logging.basicConfig(level=args.log_level)

    # Create a worker with the directive value 'echo_py'.
    worker = Ygg.Worker(directive=args.directive, remote_content=False, features=None)

    # Set a data receive handler function
    worker.set_rx_func(handle_rx)

    # Set an event receive handler function
    worker.set_event_func(handle_event)

    # Set a cancel handler function
    worker.set_cancel_func(handle_cancel)

    # Connect the worker
    worker.connect()

    # Create the main run loop
    loop = GLib.MainLoop()

    # Connect a signal handler to the worker's disconnected signal
    worker.connect_after("disconnected", disconnected_signal_handler, loop)

    # Run the main loop
    loop.run()
