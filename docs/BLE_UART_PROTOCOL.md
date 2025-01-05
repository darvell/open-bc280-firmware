# BC280 BLE/UART1 Protocol Analysis

## Overview

The BC280 firmware implements a binary protocol over UART1 for BLE communication. This protocol is used for remote control, configuration, and telemetry reporting between the display and a BLE app.

**Analysis Source:** IDA Pro database `BC280_Combined_Firmware_3.3.6_4.2.5.bin`

**Key Functions:**
- UART RX interrupt handler: `UART1_interrupt_handler` @ 0x8005928
- Frame parser: `ble_uart_frame_parser` @ 0x8011C40
- Command dispatcher: `APP_BLE_UART_CommandProcessor` @ 0x801137C
- Bootloader dispatcher: `BOOT_BLE_CommandProcessor` @ 0x8000394

---

## Frame Format

### RX/TX Frame Structure

All frames follow this format:

```
[SOF] [CMD] [LEN] [PAYLOAD...] [CHECKSUM]
```

**Field Details:**

| Offset | Field | Size | Description |
|--------|-------|------|-------------|
| 0 | SOF | 1 byte | Start of Frame marker: **0x55** |
| 1 | CMD | 1 byte | Command code (see command table) |
| 2 | LEN | 1 byte | Payload length (N bytes) |
| 3..N+2 | PAYLOAD | N bytes | Command-specific payload data |
| N+3 | CHECKSUM | 1 byte | XOR checksum (inverted) |

### Checksum Calculation

The checksum is calculated as follows (see `validate_packet_xor_checksum` @ 0x801133C and `BLE_finalize_and_send_packet` @ 0x8012B90):

```c
uint8_t checksum = 0;
for (int i = 0; i < frame_length; i++) {
    checksum ^= frame[i];  // XOR all bytes from SOF through payload
}
checksum = ~checksum;  // Invert the result
```

**Example:**
```
Frame: [0x55] [0x04] [0x00]
XOR:   0x55 ^ 0x04 ^ 0x00 = 0x51
Checksum: ~0x51 = 0xAE
Complete frame: 55 04 00 AE
```

### Buffer Limits

- **Max payload length:** 146 bytes (0x92)
- **Max total frame length:** 150 bytes
- **RX ring buffer:** 200 bytes circular buffer
- **RX frame slots:** 7 slots (ring buffer)
- **TX queue slots:** 7 slots (ring buffer)

---

## Command Reference - Application Mode

Commands processed by `APP_BLE_UART_CommandProcessor` when the device is in normal operation mode.

### 0x00 - Error Response
**Direction:** Display → BLE
**Payload:** None
**Response:** Simple error response

```
TX: [55] [01] [01] [01] [checksum]
```

### 0x02 - Authentication Challenge
**Direction:** BLE → Display
**Payload:** 9 bytes (3 key-value pairs)

This is a three-stage authentication mechanism that validates against a 768-byte lookup table at 0x802953E.

**Payload Format:**
```
[0] key1 (1-3)
[1] index1 (0-255)
[2] expected_value1
[3] key2 (1-3)
[4] index2 (0-255)
[5] expected_value2
[6] key3 (1-3)
[7] index3 (0-255)
[8] expected_value3
```

**Algorithm:**
```c
lookup_table[(key-1) * 256 + index] == expected_value
```
All three validations must succeed.

**Response on Success:**
```
TX: [55] [03] [01] [00] [checksum]
```
- Sets authentication flag: `system_data[+12] = 1`
- Schedules advanced config callback (timer slot 22, every 1s)

**Response on Failure:**
```
TX: [55] [03] [01] [01] [checksum]
```
- Schedules basic config menu callback (timer slot 25, once after 1s)

### 0x04 - Get Firmware Version
**Direction:** BLE → Display
**Payload:** None

**Response Format:**
```
TX: [55] [05] [07] [version_data...] [checksum]
```

**Response Payload (7 bytes):**
- Bytes from `system_data->pad_01C0_1[0..7]`
- Firmware version and build information

### 0x06 - Set Date/Time
**Direction:** BLE → Display
**Payload:** 7 bytes

**Payload Format:**
```
[0] padding/reserved
[1] year - 2000 (e.g., 25 for 2025)
[2] month (1-12)
[3] day (1-31)
[4] hour (0-23)
[5] minute (0-59)
[6] second (0-59)
```

**Success Response:**
```
TX: [55] [07] [01] [00] [checksum]
```

**Failure Response:**
```
TX: [55] [07] [01] [01] [checksum]
```

**Side Effects:**
- Updates RTC time and date
- May trigger trip data reset if conditions are met
- Sets time sync flag: `system_data[+0x950] = 1`
- Can trigger system mode change to mode 1

### 0x09 - Update Data Cache (Command 22)
**Direction:** BLE → Display
**Payload:** None

**Effect:**
```c
BLE_update_data_cache(22);
```

### 0x0A - Get Battery/Distance Info
**Direction:** BLE → Display
**Payload:** None

**Response Format:**
```
TX: [55] [0B] [02] [digit1] [digit2] [checksum]
```

**Response Payload:**
- `digit1`, `digit2`: Decimal digits parsed from version string (looks for '.' separator)

### 0x20 - Enter Bootloader (CRITICAL SECURITY ISSUE)
**Direction:** BLE → Display
**Payload:** None

**Effect:**
```c
write_uint16_to_storage(bootloader_marker_flash_addr, 0xAAAA);
validate_memory_address(&bootloader_stack_pointer_and_entry);
// System resets and enters bootloader mode
```

**SECURITY WARNING:** This command has NO authentication check and allows direct bootloader entry by writing magic marker 0xAAAA to flash. This is a security bypass vulnerability.

### 0x26 (38) - Reserved/No-op
**Direction:** BLE → Display
**Payload:** Any

**Effect:** None (command is ignored)

### 0x30 (48) - Get Instrument Parameters
**Direction:** BLE → Display
**Payload:** None

**Response Format:**
```
TX: [55] [31] [16] [data...] [checksum]
```

**Response Payload (22 bytes):**
```
[0-3]   speed_primary (odometer_distance) - 4 bytes BE
[4-7]   speed_secondary (odometer_moving_time_s) - 4 bytes BE
[8-11]  speed_tertiary (odometer_distance_subunits) - 4 bytes BE
[12-13] battery_voltage - 2 bytes BE
[14-15] current_amperage - 2 bytes BE
[16]    unknown_status_flag
[17]    assist_level (0-4)
[18]    headlight_enabled (0/1)
[19]    auto_poweroff_minutes
[20]    speed_limit_kph (speed_limit_kph_x10 / 10)
[21]    units_mode
[22]    reserved (0)
[23]    screen_brightness_level
```

### 0x32 (50) - Set Bike Configuration
**Direction:** BLE → Display
**Payload:** 3 bytes

**Payload Format:**
```
[0] config_type (1-7)
[1] reserved (0)
[2] config_value
```

**Configuration Types:**

**Type 1: Headlight Control**
- `value=1`: Enable headlight
  - Sets `headlight_enabled = 1`
  - Forces Shengyi DWG22 update
  - Updates light, system status, brightness flags
- `value=0`: Disable headlight
  - Sets `headlight_enabled = 0`
  - Restores saved brightness level

**Type 2: Display/UI Mode**
- Sets `auto_poweroff_minutes = value`
- Updates data cache (7)
- Schedules configurable timeout
- Sets system config update flag

**Type 3: Speed Limit**
- Sets `speed_limit_kph_x10 = value * 10`
- Sets system config update flag

**Type 4: Units Mode**
- Sets `units_mode = value`
- Sets system config update flag

**Type 5: Assist Level Adjustment**
- `value=1`: Increment assist level (max 4)
- `value=0`: Decrement assist level (min 0)
- Forces Shengyi DWG22 update on change
- Updates light, system status flags

**Type 6: Reserved**
- No operation

**Type 7: Screen Brightness**
- Sets `screen_brightness_level = value`
- Applies brightness setting immediately
- Sets system config update flag

**Response Format:**
```
TX: [55] [33] [03] [config_type] [status] [value] [checksum]
```
- `status`: 0 = success, 1 = failure
- `value`: New/current value

### 0x37 (55) - Get Instrument Group Data
**Direction:** BLE → Display
**Payload:** 1 byte (group number 1-4)

**Group 1 Response (0x38, 22 bytes):**
```
TX: [55] [38] [16] [01] [data...] [checksum]
```
Payload:
```
[0]     group_id = 1
[1-4]   speed_primary (odometer_distance) - 4 bytes BE
[5-8]   speed_secondary (odometer_moving_time_s) - 4 bytes BE
[9-12]  speed_tertiary (odometer_distance_subunits) - 4 bytes BE
[13-14] front_light_control (odometer_max_speed_kph_x10) - 2 bytes BE
[15-16] rear_light_control (odometer_avg_speed_kph_x10) - 2 bytes BE
[17]    pad_0180
[18]    assist_level
[19]    headlight_enabled
[20]    units_mode
[21]    reserved (0)
```

**Group 2 Response (0x38, 6 bytes):**
```
TX: [55] [38] [06] [02] [data...] [checksum]
```
Payload:
```
[0] group_id = 2
[1] screen_brightness_level
[2] auto_poweroff_minutes
[3] speed_limit_kph (speed_limit_kph_x10 / 10)
[4] reserved (1)
[5] reserved (1)
```

**Group 3 Response (0x38, 43 bytes):**
```
TX: [55] [38] [2B] [03] [zeros...] [checksum]
```
Payload: 42 bytes of zeros (reserved/unused data)

**Group 4 Response (0x38, 41 bytes):**
```
TX: [55] [38] [29] [04] [data...] [checksum]
```
Payload: Comprehensive odometer and trip data
```
[0]     group_id = 4
[1-4]   odometer_distance - 4 bytes BE
[5-8]   odometer_moving_time_s - 4 bytes BE
[9-12]  odometer_distance_subunits - 4 bytes BE
[13-14] odometer_max_speed_kph_x10 - 2 bytes BE
[15-16] odometer_avg_speed_kph_x10 (calculated: distance*10/time) - 2 bytes BE
[17-20] odometer_co2_saved (distance * 100.0) - 4 bytes BE
[21-22] odometer_calories (distance * 10.0) - 2 bytes BE
[23-26] tripA_distance - 4 bytes BE
[27-30] tripA_moving_time_s - 4 bytes BE
[31-34] tripA_distance_subunits - 4 bytes BE
[35-36] tripA_avg_speed (calculated) - 2 bytes BE
[37-40] tripB_distance - 4 bytes BE (continues...)
```

### 0x60 (96) - Get Realtime Motion Data
**Direction:** BLE → Display
**Payload:** None

**Response Format:**
```
TX: [55] [61] [0B] [data...] [checksum]
```

**Response Payload (11 bytes):**
```
[0-1]   battery_power_W - 2 bytes BE
[2-3]   motor_temperature - 2 bytes BE
[4]     assist_level (0-4)
[5-6]   motor_speed (speed_kph_x10) - 2 bytes BE
[7-10]  speed_primary (odometer_distance) - 4 bytes BE
[11]    pad_026E
[12]    motor_error_flags
[13-14] motor_tick_period_ms - 2 bytes BE
```

### 0x63 (99) - Get History Data (Current)
**Direction:** BLE → Display
**Payload:** None

**Effect:**
- Updates data cache entries 23, 24
- Sends history data response for current pointer position
- If more data exists, schedules delayed callback (slot 0x17, 3000ms)

**Response Format:**
```
TX: [55] [64] [0A] [data...] [checksum]
```

**Response Payload (10 bytes):**
```
[0-3]   timestamp - 4 bytes BE (UTC - 28800, adjusted)
[4-5]   data field 1 - 2 bytes
[6-7]   data field 2 - 2 bytes
[8-9]   data field 3 - 2 bytes
[10]    data field 4
[11-12] data field 5 - 2 bytes
```

### 0x65 (101) - Get History Data (Next)
**Direction:** BLE → Display
**Payload:** None

**Effect:**
- Updates data cache entries 23, 24
- Advances history data pointer
- Sends history data response
- If more data exists, schedules delayed callback (slot 0x17, 3000ms)

Response format: Same as command 0x63

### 0x67 (103) - Get Motor Data (Current)
**Direction:** BLE → Display
**Payload:** None

**Effect:**
- Updates data cache entries 23, 24
- Sends motor/trip data response

**Response Format:**
```
TX: [55] [68] [16+N] [data...] [checksum]
```

**Response Payload (22 + N bytes):**
```
[0-3]   start_timestamp - 4 bytes BE (UTC - 28800)
[4-7]   end_timestamp - 4 bytes BE (UTC - 28800)
[8-9]   field1 - 2 bytes
[10-11] field2 - 2 bytes
[12-13] avg_value1 (calculated with float math) - 2 bytes BE
[14-17] field3 - 4 bytes
[18-19] field4 - 2 bytes
[20]    field5
[21-24] ... (additional computed fields)
[22+]   N additional bytes (variable length based on data)
```

### 0x69 (105) - Get Motor Data (Next Trip)
**Direction:** BLE → Display
**Payload:** None

**Effect:**
- Updates data cache entries 23, 24
- Advances trip data pointer
- Sends motor data response

Response format: Same as command 0x67

### 0xF0 (240) - Get Battery Stats
**Direction:** BLE → Display
**Payload:** None

**Response Format:**
```
TX: [55] [F1] [08] [data...] [checksum]
```

**Response Payload (8 bytes):**
```
[0-1]   reserved (0x00, 0x00)
[2-3]   battery_voltage_mV - 2 bytes BE
[4-5]   reserved (0x00, 0x00)
[6-7]   battery_current_mA - 2 bytes BE
```

---

## Command Reference - Bootloader Mode

Commands processed by `BOOT_BLE_CommandProcessor` when the device is in bootloader mode.

### 0x00 - Bootloader Ping
**Direction:** BLE → Display
**Payload:** None

**Response:**
```
TX: [55] [01] [01] [00] [checksum]
```

### 0x02 - Bootloader Authentication
**Direction:** BLE → Display
**Payload:** 9 bytes (same format as app mode 0x02)

**Success Response:**
```
TX: [55] [03] [01] [00] [checksum]
```
- Removes task ID 10

**Failure Response:**
```
TX: [55] [03] [01] [01] [checksum]
```

### 0x04 - Get Bootloader Firmware Version
**Direction:** BLE → Display
**Payload:** None

**Response:**
```
TX: [55] [05] [payload...] [checksum]
```
Calls `ble_send_firmware_version()` @ 0x80009F0

### 0x06 - Bootloader Time Sync
**Direction:** BLE → Display
**Payload:** None

**Response:**
```
TX: [55] [07] [01] [00] [checksum]
```

### 0x20 (32) - Bootloader Reset/ACK
**Direction:** BLE → Display
**Payload:** None

**Response:**
```
TX: [55] [21] [00] [checksum]
```

### 0x22 (34) - Firmware Update Initialization
**Direction:** BLE → Display
**Payload:** 5 bytes

**Payload Format:**
```
[0]     expected_crc8
[1-4]   firmware_size - 4 bytes BE
```

**Validation:**
1. Checks firmware_size <= APP_IMAGE_SIZE (typically 128KB)
2. Calls `flash_verify_sectors()`
3. Calls `flash_check_erased()` to ensure flash is empty

**Success Response:**
```
TX: [55] [23] [01] [01] [checksum]
```
- Stores `g_ble_fw_update_size_bytes = firmware_size`
- Stores `g_ble_fw_update_expected_crc8 = expected_crc8`
- Displays "BLE Updating..." on LCD
- Starts UI cycling task (ID 11, 1000ms interval)

**Failure Response:**
```
TX: [55] [23] [01] [00] [checksum]
```

### 0x24 (36) - Firmware Write Block
**Direction:** BLE → Display
**Payload:** 132 bytes

**Payload Format:**
```
[0-3]   block_index - 4 bytes BE
[4-131] data - 128 bytes
```

**Operation:**
```c
flash_address = APP_IMAGE_BASE + (block_index * 128);
flash_write_buffer(flash_address, data, 128);
```

**Success Response:**
```
TX: [55] [25] [01] [01] [checksum]
```

**Failure Response:**
```
TX: [55] [25] [01] [00] [checksum]
```

### 0x26 (38) - Firmware Update Complete
**Direction:** BLE → Display
**Payload:** None

**Operation:**
1. Calculates CRC8 over `g_ble_fw_update_size_bytes` bytes
2. Compares with `g_ble_fw_update_expected_crc8`
3. Stops UI cycling task (ID 11)

**Success Response:**
```
TX: [55] [27] [01] [01] [checksum]
```
- Writes `0xFFFF` to `g_bootloader_mode_flag` (exits bootloader mode)
- Displays "BLE Update Success!" on LCD
- Schedules firmware validation task (ID 6, 2000ms delay)

**Failure Response:**
```
TX: [55] [27] [01] [00] [checksum]
```
- Displays "BLE Update failed!" on LCD

---

## Implementation Details

### Receive Path

**UART1 Interrupt Handler** (`UART1_interrupt_handler` @ 0x8005928):

1. **RX Interrupt (IT 1317):**
   - Reads byte from USART data register
   - Stores in 250-byte circular buffer
   - Advances write pointer (mod 250)

2. **TX Interrupt (IT 1574):**
   - Transmits queued TX data
   - Manages 5-slot TX queue (81 bytes each)
   - Disables TX interrupt when queue is empty

3. **Error Handling (IT 864):**
   - Clears error flags
   - Discards received byte

**Frame Parser** (`ble_uart_frame_parser` @ 0x8011C40):

```c
struct ble_frame_slot_t {
    uint8_t sof;              // 0x55
    uint8_t cmd;
    uint8_t payload_len;
    uint8_t payload[146];
    uint8_t _pad0;
    uint16_t len;             // Current frame length
};

struct ble_app_comm_context_t {
    uint16_t rx_raw_rd_idx;       // Raw RX buffer read index
    uint16_t rx_raw_wr_idx;       // Raw RX buffer write index
    uint16_t rx_raw_parse_idx;    // Parse position
    uint8_t  rx_raw_ring[200];    // 200-byte circular buffer

    uint16_t rx_read_index;       // Frame read index (0-6)
    uint16_t rx_write_index;      // Frame write index (0-6)
    ble_frame_slot_t rx_frames[7]; // 7 frame slots

    // TX queue (similar structure)
};
```

**Parser Algorithm:**
1. Wait for SOF byte (0x55)
2. Check if next frame slot is available (write_idx+1 != read_idx)
3. Accumulate bytes into current slot
4. When length >= 4, check if payload_len > 146 (discard if invalid)
5. When all bytes received (payload_len + 4), validate checksum
6. If valid, advance RX write pointer
7. If invalid, discard frame and advance read pointer by 1

### Transmit Path

**TX Buffer Add** (`BLE_tx_buffer_add_byte` @ 0x8012B38):

```c
void BLE_tx_buffer_add_byte(uint8_t byte) {
    uint8_t *current_packet = &tx_buffer_pool[154 * tx_write_idx];

    if ((tx_write_idx + 1) % 7 != tx_read_idx &&  // Check queue not full
        current_packet[76] < 0x96) {                // Check length < 150
        current_packet[current_packet[76] + 2] = byte;
        current_packet[76]++;  // Increment length
    }
}
```

**Finalize and Send** (`BLE_finalize_and_send_packet` @ 0x8012B90):

```c
void BLE_finalize_and_send_packet() {
    uint8_t *packet = &tx_buffer_pool[154 * tx_write_idx];
    int length = packet[76];

    if (length >= 3 && length < 0x96) {
        // Set payload length byte
        packet[4] = length - 3;

        // Calculate checksum
        uint8_t checksum = 0;
        for (int i = 0; i < length; i++) {
            checksum ^= packet[i + 2];
        }

        // Add inverted checksum
        packet[length + 2] = ~checksum;
        packet[76]++;  // Include checksum in length

        // Advance write pointer
        tx_write_idx = (tx_write_idx + 1) % 7;
    }
}
```

**TX Queue Processing** (`process_tx_buffer_queue`):

The function `process_incoming_uart_commands` @ 0x8011A3C calls:
1. `ble_uart_frame_parser()` - Parse incoming bytes
2. `APP_BLE_UART_CommandProcessor()` - Process commands
3. `process_tx_buffer_queue()` - Send queued responses

---

## Memory Layout

### SRAM Addresses

Based on decompiled code references:

```
0x200017B4: BLE TX queue control structure
0x20001CBC: BLE TX buffer pool (7 slots × 154 bytes = 1078 bytes)
```

### Flash Addresses

```
0x0802953E: Authentication lookup table (768 bytes = 3 × 256)
            Used for 3-stage authentication in command 0x02
```

---

## Security Analysis

### Critical Vulnerabilities

**1. Unauthenticated Bootloader Entry (Command 0x20)**
- **Severity:** CRITICAL
- **Issue:** No authentication check before writing bootloader marker
- **Impact:** Any BLE client can force device into bootloader mode
- **Code:** `write_uint16_to_storage(bootloader_marker_flash_addr, 0xAAAA);`

**2. Authentication Bypass Potential**
- **Severity:** HIGH
- **Issue:** Authentication lookup table at fixed flash address 0x802953E
- **Impact:** If table is extracted, authentication can be precomputed
- **Note:** System data flag at offset +12 controls advanced features

### Authentication Mechanism

The 3-stage authentication (command 0x02) validates against a static lookup table:

```c
bool authenticate(uint8_t key1, uint8_t idx1, uint8_t val1,
                  uint8_t key2, uint8_t idx2, uint8_t val2,
                  uint8_t key3, uint8_t idx3, uint8_t val3) {
    return (auth_table[(key1-1)*256 + idx1] == val1) &&
           (auth_table[(key2-1)*256 + idx2] == val2) &&
           (auth_table[(key3-1)*256 + idx3] == val3);
}
```

**Lookup Table Structure:**
- Total size: 768 bytes (3 keys × 256 entries)
- Key 1: Bytes 0-255
- Key 2: Bytes 256-511
- Key 3: Bytes 512-767

---

## Usage Examples

### Ping/Version Request
```
TX: 55 04 00 AB
RX: 55 05 07 [7 bytes version] [checksum]
```

### Set Date/Time (2025-03-15 14:30:00)
```
TX: 55 06 07 00 19 03 0F 0E 1E 00 [checksum]
    |  |  |  |  |  |  |  |  |  |
    |  |  |  |  |  |  |  |  |  +- Second (0)
    |  |  |  |  |  |  |  |  +---- Minute (30)
    |  |  |  |  |  |  |  +------- Hour (14)
    |  |  |  |  |  |  +---------- Day (15)
    |  |  |  |  |  +------------- Month (3)
    |  |  |  |  +---------------- Year-2000 (25)
    |  |  |  +------------------- Reserved (0)
    |  |  +---------------------- Payload length (7)
    |  +------------------------- Command (0x06)
    +---------------------------- SOF (0x55)
```

### Get Realtime Motion Data
```
TX: 55 60 00 A5
RX: 55 61 0B [11 bytes data] [checksum]
```

### Set Headlight On
```
TX: 55 32 03 01 00 01 [checksum]
    |  |  |  |  |  |
    |  |  |  |  |  +- Value (1 = on)
    |  |  |  |  +---- Reserved (0)
    |  |  |  +------- Config type (1 = headlight)
    |  |  +---------- Payload length (3)
    |  +------------- Command (0x32)
    +---------------- SOF (0x55)

RX: 55 33 03 01 00 01 [checksum]
    |  |  |  |  |  |
    |  |  |  |  |  +- New value (1)
    |  |  |  |  +---- Status (0 = success)
    |  |  |  +------- Config type (1)
    |  |  +---------- Payload length (3)
    |  +------------- Response command (0x33)
    +---------------- SOF (0x55)
```

### Firmware Update Flow
```
1. Init: 55 22 05 [crc8] [size_BE32] [checksum]
   Resp: 55 23 01 01 [checksum]  (accepted)

2. Write blocks (repeat for each 128-byte block):
   TX: 55 24 84 [idx_BE32] [128 bytes data] [checksum]
   RX: 55 25 01 01 [checksum]  (success)

3. Complete: 55 26 00 [checksum]
   Resp: 55 27 01 01 [checksum]  (CRC OK, update success)
```

---

## Implementation Notes for Host Simulator

### Critical Requirements

1. **Always use 0x55 as SOF**
2. **Calculate inverted XOR checksum correctly**
3. **Respect max payload length of 146 bytes**
4. **Use big-endian for multi-byte values** (except where noted)
5. **Wait for response before sending next command** (queue size is limited)

### Recommended Simulator Architecture

```python
class BLEProtocol:
    SOF = 0x55
    MAX_PAYLOAD = 146

    def build_frame(self, cmd, payload):
        frame = bytearray([self.SOF, cmd, len(payload)])
        frame.extend(payload)

        # Calculate XOR checksum
        checksum = 0
        for byte in frame:
            checksum ^= byte
        frame.append(~checksum & 0xFF)

        return bytes(frame)

    def parse_frame(self, data):
        if len(data) < 4 or data[0] != self.SOF:
            return None

        payload_len = data[2]
        if len(data) < payload_len + 4:
            return None  # Incomplete

        # Validate checksum
        checksum = 0
        for i in range(payload_len + 3):
            checksum ^= data[i]

        if (~checksum & 0xFF) != data[payload_len + 3]:
            return None  # Invalid checksum

        return {
            'cmd': data[1],
            'payload': data[3:3+payload_len]
        }
```

### Common Pitfalls

1. **Forgetting to invert checksum** - Must use `~checksum`, not just `checksum`
2. **Wrong endianness** - Multi-byte values are typically big-endian
3. **Payload length confusion** - `len` field is payload only, not including header/checksum
4. **Buffer overflow** - Firmware validates payload_len <= 146 and discards invalid frames
5. **Queue overflow** - Only 7 TX slots; wait for responses before flooding with commands

---

## Related Functions

### Key Helper Functions

- `BLE_update_data_cache(index)` - Updates cached telemetry field
- `get_assist_level()` - Returns current assist level (0-4)
- `set_system_config_update_flag(1)` - Marks config as dirty
- `shengyi_set_send_force_flag(1)` - Forces update to Shengyi DWG22 motor controller
- `schedule_delayed_callback()` - Schedules timer-based callbacks
- `datetime_to_timestamp()` / `timestamp_to_datetime()` - Time conversion

### System Data Offsets (Approximate)

Based on decompiled code references:
```
+12  (0x00C): Authentication success flag
+25  (0x019): Motor temperature
+40  (0x028): Battery power (W)
+157 (0x09D): Major instrument data (odometer distance)
+169 (0x0A9): Odometer moving time
+173 (0x0AD): Odometer distance subunits
+177 (0x0B1): Front light control
+181 (0x0B5): Rear light control
+193 (0x0C1): Battery voltage
+418 (0x1A2): Auth flag (system_data + 418)
+0x950: Time sync flag
```

### Config Data Structure
```c
struct system_config {
    uint8_t headlight_enabled;        // +0
    uint8_t screen_brightness_level;  // +?
    uint8_t auto_poweroff_minutes;    // +?
    uint16_t speed_limit_kph_x10;     // +?
    uint8_t units_mode;               // +?
    uint8_t assist_level_cmd;         // +?
    // ... more fields
};
```

---

## Conclusion

This protocol analysis provides complete documentation of the BC280 BLE/UART1 protocol for implementing a host-mode simulator. The protocol is well-structured but has critical security vulnerabilities (unauthenticated bootloader entry) that should be addressed in production firmware.

For simulator implementation, focus on:
1. Correct frame formatting with 0x55 SOF and inverted XOR checksum
2. Proper big-endian encoding of multi-byte values
3. Response handling with appropriate delays
4. Error handling for invalid/incomplete frames

**Generated:** 2025-12-24
**Tool:** IDA Pro MCP Server
**Database:** BC280_Combined_Firmware_3.3.6_4.2.5.bin
