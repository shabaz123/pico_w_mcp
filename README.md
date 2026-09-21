# Pico W MCP Demo

This simple demo project implements an MCP Server inside a Pi Pico W. Use the Pi Pico C/C++ SDK to build the code.

Here is how to use the code in a project, to provide (say) a relay and a temperature sensor:

```cpp
MCPServer mcp("pico_w_mcp");

mcp.tool(
    "set_relay",
    "Turn the Pico W built-in LED on or off.",
    setRelay
).param<bool>(
    "state",
    "True turns the relay on; false turns it off."
);

mcp.resource(
    "sensor://temperature",
    "temperature",
    "Current temperature in degrees Celsius.",
    readTemperature
);

mcp.begin(8000);
```

Callbacks:

```cpp
static bool setRelay(MCPArgs &args, char *out, size_t out_size, void *) {
    bool state = args.getBool("state");
    // operate hardware...
    snprintf(out, out_size, "Relay is %s", state ? "ON" : "OFF");
    return true;
}

static bool readTemperature(char *out, size_t out_size, void *) {
    snprintf(out, out_size, "23.5 degrees C");
    return true;
}
```

The library automatically builds tools/list and resources/list from the registered objects.

The project has intentionally a small prototype parser, not a general-purpose JSON implementation.

# Building the Code

Please consult the official Pi Pico C/C++ SDK documentation. 

# Usage

Once the code has been built (you'll need to pass your 2.4 GHz WLAN SSID and password during the build, since this is just a demo experimental project), install it on the Pi Pico (hold down the BOOTSEL button which inserting the USB cable, and then release the button, then drag the .uf2 firmware file onto the drive letter that appeared), then use your network tools to find out what IP address has been assigned to your Pi Pico.

If you're using Antigravity CLI, then type the following (change the IP address to suit):

`agy mcp add --type http pico_w_mcp http://192.168.1.159:8000/mcp`

Next, run Antigravity, and type the following:

> Turn on the relay, then get the temperature.

You should see the following:

```
Thought for 1s, 369 tokens
I'm currently checking available MCP tools and skills and Python scripts. I'm focusing on the prompt t...
Read(~/.gemini/antigravity-cli/mcp/pico_w_mcp/set_relay.json)
● Bash(Get-ChildItem C:\Users\shabaz.gemini\antigravity-cli\mcp\pico_w_mcp)
● Bash(Get-ChildItem C:\dev\projects\junk8\junk9)
● pico_w_mcp/set_relay(Turn relay on) (ctrl+o to expand)
I have turned on the relay and checked the temperature sensor.
### Actions & Results

1. Relay: Turned ON (successfully updated state).
2. Temperature: 23.5 °C (retrieved from sensor://temperature).

```

<br />

