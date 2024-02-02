from enum import IntEnum
from typing import Any, Callable, Optional, TypeAlias

from gi.repository import GLib, GObject

YGG_DISPATCHER_EVENT_RECEIVED_DISCONNECT: int = 1
YGG_DISPATCHER_EVENT_UNEXPECTED_DISCONNECT: int = 2
YGG_DISPATCHER_EVENT_CONNECTION_RESTORED: int = 3

UUID: TypeAlias = str

RxFunc: TypeAlias = Callable[["Worker", str, UUID, Optional[UUID], Optional[dict[str, Any]], Any], None]
EventFunc: TypeAlias = Callable[["WorkerEvent", Any], None]


class YggMetadata:
    """Key-value store."""

    def get(self, key: str) -> Optional[str]:
        """Look up a value in the store."""
    
    def set(self, key: str, value: str) -> bool:
        """Insert a value into the store.
        
        :returns: `True` if the key did not exist, `False` otherwise.
        """


class WorkerEvent(IntEnum):
    """Worker signals.
    
    Signals emitted by Worker instances to indicate the operating state of
    the worker to interested parties.
    """
    BEGIN: int = 1
    """Worker has accepted the data and is beggining to process it."""
    END: int = 2
    """Worker has completed processing on the last received data."""
    WORKING: int = 3
    """Worker is processing the data."""


class Worker(GObject.GPointer):
    def __init__(self, directive: str, remote_content: bool, features: Optional[dict[str, Any]]) -> None:
        """Create new worker.

        :param directive:
            The worker directive name.
            This is used to create the D-Bus interface path when joined with
            prefix `com.redhat.Yggdrasil1.Worker1.`.
        :param remote_content:
            The worker requires content from a remote URL.
        :param features:
            An initial table of values to use as the worker's feature map.
        :raises gi.repository.GLib.GError:
            `directive` is not valid directive;
            `features` is not `None` or a valid dictionary.
        """

    def connect(self) -> bool:
        """Connects to the D-Bus bus.

        The worker connects to either a system or session D-Bus connection.
        
        It then exports itself onto the bus, implementing the
        `com.redhat.Yggdrasil1.Worker1` interface.

        :returns: `True` if the connection was successful.
        :raises gi.repository.GLib.GError: The connection was not successful.
        """

    def transmit(
        self,
        addr: str,
        id: UUID,
        response_to: Optional[UUID],
        metadata: Optional[dict[str, Any]],
        data: bytes,
        cancellable: GLib.Cancellable,
        callback: GLib.GAsyncReadyCallback,
    ) -> None:
        """Transmits the data.

        Invokes the `com.redhat.Yggdrasil1.Dispatcher1.Transmit` D-Bus method
        asynchronously. To receive the response from the D-Bus method, call
        `transmit_finish`.

        :param addr: Destination address of the data to be transmitted.
        :param id: A UUID.
        :param response_to: A UUID the data is in response to.
        :param metadata: Key/value pairs associated with the data.
        :param data: The data.
        :param cancellable:
        :param callback:
            A callback function to be invoked when the task is complete.
        """

    def transmit_finish(
        self,
        res: GLib.GAsyncResult,
        response_code: int,
        response_metadata: YggMetadata,
        response_data: GLib.Bytes,
    ) -> bool:
        """Finishes transmitting the data.

        :param res: The result.
        :param response_code: An integer status code.
        :param response_metadata: A map of key/value pairs.
        :param response_data: Data received in response to the transmit.
        :returns: `True` on success, `False` on error.
        """

    def emit_event(self, event: WorkerEvent, message_id: str, message: Optional[str]) -> bool:
        """Emit a `com.redhat.Yggdrasil1.Worker`.Event` signal.
        
        :param event: The event to emit.
        :param message_id: The message ID.
        :param message: The message to include with the emitted signal.
        :returns: `True` on success, `False` on error.
        """

    def get_feature(self, key: str) -> str:
        """Looks up a value in the features table.

        :param key: The key to look up in the features table.
        :raises gi.repository.GLib.GError: Key is not present.
        """

    def set_feature(self, key: str, value: str) -> bool:
        """Stores the value.

        Emits a signal on the worker's D-Bus path and interface
        `org.freedesktop.DBus.Properties` as `PropertiesChanged`.
        
        :returns: `True` if the key did not exist.
        """

    def set_rx_func(
        self,
        func: RxFunc,
    ) -> bool:
        """Sets a callable that is invoked when data is received.

        :param func: The callback function.
        :returns: `True` when the callable is saved.
        """

    def set_event_func(
        self,
        func: EventFunc,
    ) -> bool:
        """Sets a callable that is invoked when an event is received.
        
        :param func: The callback function.
        :returns: `True` if setting the function handler succeeded.
        """
