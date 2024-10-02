# winlirc-client

WinLIRC Client is a small program that allows you to simulate key presses based on the received commands of your IR remote control.

Requires WinLIRC to be installed and configured by the user - this client program only interfaces with it.

## Configuration Files

### config.json - main configuration file

```json
{
    "key_timeout": 125,
    "key_repeat_delay": 4,
    "lirc_host": "127.0.0.1",
    "lirc_port": 8765,
    "lirc_rc_attempts": 0,
    "lirc_rc_interval": 1000
}
```

- `key_timeout`: Timeout in milliseconds before the last received key is considered released (while no new commands received, otherwise release event is handled by the client automatically). This value should be at least 10-20% bigger than the time delay between IR commands your remote sends.
- `key_repeat_delay`: A delay before keys begin to repeat, measured in the number of received commands.
- `lirc_host`: Address of the WinLIRC server.
- `lirc_port`: TCP/IP port.
- `lirc_rc_attempts`: Number of attempts this client will perform trying to connect/reconnect to the server. 0 = unlimited.
- `lirc_rc_interval`: Time interval between attempts, in milliseconds.

### keymap.json - key mapping file

```json
{
    "EXAMPLE": {
        "vkCode": 21,
        "withShift": false,
        "withCtrl": false,
        "withAlt": false
    }
}
```

- `"EXAMPLE"`: Section representing key name configured in WinLIRC.
- `vkCode`: Virtual key code, can be in decimal (as int) or in hex format (string).
  A complete list of codes can be found here: https://learn.microsoft.com/en-us/windows/desktop/inputdev/virtual-key-codes
- `withShift`, `withCtrl`, `withAlt`: Should be self-explanatory; can be used if the button is a shortcut, not just a key.

## Tips

If you experience slowness, delays, lags, or weird behavior of this program or WinLIRC:

1. Set their process priorities to REALTIME.
   - This solves all the issues.
   - Don't worry about CPU usage; both WinLIRC and this client consume virtually no system resources.
2. You can do this automatically using Process Hacker or System Explorer programs (they can "save" the process priorities).

## Note

This program uses the `SendInput` function to simulate key pressing. It may not work with Direct Input or low-level keyboard interfaces, as these require a kernel driver emulating the keyboard hardware. There are no plans to implement this functionality.
