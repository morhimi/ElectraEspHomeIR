# ElectraEspHomeIR

An [ESPHome](https://esphome.io/) external component for controlling **Electra**, **Airwell**, and **Emailair** air conditioners using the 34-bit Manchester-coded infrared protocol (commonly found on **RC-3**, **RC-4**, and **RC-7** remote controls).

Built upon ESPHome's [`climate_ir`](https://esphome.io/components/climate/climate_ir.html) framework, with bidirectional state tracking using an infrared receiver.

---

## Features

- **Bidirectional State Sync:** Transmit commands to the AC and decode incoming signals from physical handheld remotes to keep Home Assistant in sync.
- **Modes:** Cool, Heat, Auto (Heat/Cool), Dry, Fan Only, and Off.
- **Fan Speeds:** Low, Medium, High, and Auto.
- **Presets & Controls:** Sleep mode preset, vertical swing toggle.
- **Dedicated Off Command Support:** Configurable support for units with an explicit Off code (`supports_off_command`).
- **State Synchronization Action:** Built-in `electra.sync_state` action to manually resync state if the IR receiver missed a physical remote button press.
- **iFeel Telemetry Protection:** Safely ignores periodic remote iFeel telemetry broadcasts so target temperature setpoints are not overwritten.

---

## Installation

Add this repository as an `external_component` in your ESPHome configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/omerstr/ElectraEspHomeIR
      ref: main
```

---

## Configuration Example

```yaml
remote_transmitter:
  pin: GPIO4
  carrier_duty_percent: 50%
  id: ir_transmitter

remote_receiver:
  pin:
    number: GPIO5
    inverted: true
    mode:
      input: true
      pullup: true
  tolerance: 50%
  idle: 25ms
  id: ir_receiver

climate:
  - platform: electra
    name: "Living Room AC"
    id: electra_ac
    transmitter_id: ir_transmitter
    receiver_id: ir_receiver
    supports_off_command: true  # Recommended: try True first; change to False if unit does not respond to off
    # Optional external temperature sensor:
    # sensor: room_temperature
```

> [!TIP]
> Setting `tolerance: 50%` and `idle: 25ms` on the `remote_receiver` is recommended to accommodate timing variations and pulse distortion across different IR receiver modules (e.g. VS1838B / TSOP38238).

---

## Configuration Variables

- **`transmitter_id`** (**Required**, ID): The ID of the `remote_transmitter` component.
- **`receiver_id`** (*Optional*, ID): The ID of the `remote_receiver` component for listening to physical remote transmissions.
- **`supports_off_command`** (*Optional*, boolean, default: `true`): Most Electra ACs use a toggle bit for power, but many units also support a dedicated Off command that powers down the AC without toggling it back on if already off. If your unit does not turn off, set this to `false`.
- **`sensor`** (*Optional*, ID): An external temperature sensor (e.g., from Home Assistant) to display current room temperature.
- Standard options supported by [`climate_ir`](https://esphome.io/components/climate/climate_ir.html) also apply.

---

## Manual State Synchronization

If someone operates the AC using a remote out of range of the ESPHome IR receiver, you can resync the tracked state without transmitting any IR pulses using the `electra.sync_state` action:

```yaml
api:
  actions:
    - action: sync_electra_state
      then:
        - electra.sync_state: electra_ac
```

This can be called from Home Assistant via `action: esphome.<device_name>_sync_electra_state`.

You can also trigger it from an ESPHome physical button or template button:

```yaml
button:
  - platform: template
    name: "Resync AC State"
    on_press:
      - electra.sync_state: electra_ac
```

---

## Credits

- Based on [liads' ElectraClimate](https://gist.github.com/liads/c702fd4b8529991af9cd52d03b694814) gist with receiving capabilities, enhancements, and external component migration by [@omerstr](https://github.com/omerstr).
