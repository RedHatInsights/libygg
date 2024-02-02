# Echo Python worker

_echo-py_ is an example "echo" yggdrasil worker, written in Python, that uses
_libygg_ through gobject-introspection to communicate with `yggd` over D-Bus.
`main.py` is documented with comments demonstrating how to create and connect a
worker using the API.

## Setting up

```shell
$ sudo dnf install python3-gobject
$ python3 -m venv --system-site-packages .venv && source .venv/bin/activate
$ DBUS_STARTER_BUS_TYPE=session python3 main.py
```
