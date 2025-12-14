# GPS/GNSS System Documentation

## Overview

The Walter firmware includes an integrated GPS/GNSS system that automatically manages positioning fixes with seamless LTE coordination. The system is designed to work with the Sequans Monarch 2 modem's GNSS capabilities.

## Architecture

### Components

1. **womo_gps Module** (`components/womo_gps/`)
   - Low-level GNSS interface to Walter modem
   - Orchestrates complete fix cycle with LTE coordination
   - Handles time synchronization and fix optimization

2. **GPS Task** (`main.cpp`)
   - Periodic GNSS fix execution
   - State management and telemetry publishing
   - Integration with RS485 output

3. **LTE Coordination**
   - Automatic LTE disable during GNSS operations
   - Network time sync before fixes
   - Seamless LTE re-enable after fixes

## Configuration

### walter_config.h Settings

```c
// Enable GPS subsystem
#define WALTER_ENABLE_GPS 1

// Fix interval (milliseconds)
// Default: 3600000 (1 hour)
#define WALTER_GPS_FIX_INTERVAL_MS 3600000U

// Task configuration
#define WALTER_GPS_TASK_STACK 4096
#define WALTER_GPS_TASK_PRIORITY 5
```

## Usage

### Basic Operation

The GPS system operates automatically once enabled:

1. **Startup**: GPS task starts 10 seconds after system boot
2. **Fix Cycle**: Executes complete GNSS fix cycle
3. **Wait**: Delays for configured interval
4. **Repeat**: Continues indefinitely

### Fix Cycle Workflow

Each GPS fix cycle follows this sequence:

```
┌─────────────────────────────────────────────────┐
│ 1. Fetch Time from LTE Network                  │
│    - While LTE is still connected               │
│    - Improves GNSS fix speed                    │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│ 2. Set GNSS Subsystem Time                      │
│    - Syncs GNSS clock with network time         │
│    - Enables hot-start mode for faster fixes    │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│ 3. Disable LTE Radio                            │
│    - Required: GNSS and LTE share radio         │
│    - Wait 2 seconds for clean shutdown          │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│ 4. Request GNSS Fix                             │
│    - Hot start: ~60 second timeout              │
│    - Cold start: ~180 second timeout            │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│ 5. Wait for Fix Complete                        │
│    - Event-driven via callback                  │
│    - Detailed progress logging                  │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│ 6. Re-enable LTE Radio                          │
│    - Restore normal LTE operation               │
│    - Wait 5 seconds for reconnection            │
└─────────────────────────────────────────────────┘
```

## API Reference

### womo_gps Module Functions

#### Initialization
```c
esp_err_t womo_gps_init(void);
```
Initializes GPS subsystem. Called automatically by GPS task.

#### Fix Cycle Execution
```c
esp_err_t womo_gps_execute_fix_cycle(void);
```
Executes complete GNSS fix cycle with LTE coordination. This is a blocking operation that may take 30-180 seconds.

**Returns:**
- `ESP_OK` - Fix completed successfully
- `ESP_ERR_TIMEOUT` - Fix timeout (continue LTE re-enable)
- `ESP_FAIL` - Critical failure

#### Data Retrieval
```c
esp_err_t womo_gps_get_last_fix(womo_gps_data_t *data);
bool womo_gps_is_valid(void);
```
Retrieve last successful GPS fix data.

#### LTE Control Registration
```c
typedef esp_err_t (*womo_gps_lte_control_cb_t)(bool enable);
esp_err_t womo_gps_register_lte_control(womo_gps_lte_control_cb_t callback);
```
Register callback for LTE control. Used internally by GPS task.

### GPS Data Structure

```c
typedef struct {
    bool valid;                 // True if fix is valid
    double latitude;            // Degrees (-90 to +90)
    double longitude;           // Degrees (-180 to +180)
    double altitude_m;          // Meters above sea level
    float speed_kmh;            // Ground speed (km/h)
    float heading_deg;          // Heading (0-360)
    uint8_t satellites;         // Number of satellites
    float confidence_m;         // Horizontal confidence (meters)
    int64_t timestamp;          // Unix timestamp
    uint32_t time_to_fix_ms;    // Time to acquire fix (ms)
} womo_gps_data_t;
```

## Telemetry Output

### RS485 Full Telemetry

GPS data is included in the RS485 "full" telemetry packet:

```json
{
  "type": "full",
  "ts": 123456789,
  "gps": {
    "lat": 52.520008,
    "lon": 13.404954,
    "alt_m": 34.5,
    "speed_kmh": 12.3,
    "heading_deg": 45.2,
    "hdg": "NO",
    "sats": 12,
    "conf_m": 3.5,
    "ttf_ms": 45000,
    "fix_count": 42,
    "ts": 1702512345
  }
}
```

### Shared State

GPS data is available in the shared sensor state:

```c
sensor_shared_state_t state = sensor_state_snapshot();
if (state.gps.valid) {
    printf("Position: %.6f, %.6f\n", 
           state.gps.latitude, 
           state.gps.longitude);
}
```

## Logging

### Log Tags

- `womo_gps` - GPS module operations
- `womo_gps_task` - GPS task lifecycle

### Example Log Output

```
I (12345) womo_gps_task: === Starting GPS fix cycle #1 ===
I (12346) womo_gps: === Starting complete GNSS fix cycle ===
I (12347) womo_gps: Step 1: Fetching time from LTE network
I (12348) womo_gps: Fetched time from LTE network: 1702512345
I (12349) womo_gps: Set GNSS UTC time to 1702512345
I (12350) womo_gps: Step 2: Disabling LTE for GNSS operation
I (12351) walter_main: GPS module requesting LTE disable
I (14351) womo_gps: LTE disabled successfully
I (14352) womo_gps: Step 3: Requesting GNSS fix
I (14353) womo_gps: Starting GNSS fix request (attempt 1/5)
I (14354) womo_gps: GNSS fix request sent successfully (attempt 1/5)
I (14355) womo_gps: Waiting for GNSS event callback...
I (24355) womo_gps: Waiting for GNSS fix... 10/60 seconds
I (34355) womo_gps: Waiting for GNSS fix... 20/60 seconds
I (45678) womo_gps: GNSS fix received: status=1, conf=3.50, lat=52.520008, lon=13.404954, sats=12
I (45679) womo_gps: GNSS fix received after 31324 ms
I (45680) womo_gps: GNSS fix completed successfully
I (45681) womo_gps: Step 4: Re-enabling LTE
I (45682) walter_main: GPS module requesting LTE enable
I (50682) womo_gps: LTE re-enabled successfully
I (50683) womo_gps: === GNSS fix cycle complete: SUCCESS ===
I (50684) womo_gps_task: GPS fix #1 successful: lat=52.520008, lon=13.404954, alt=34.5 m, sats=12, conf=3.5 m
I (50685) womo_gps_task: Next GPS fix in 3600 seconds
```

## Troubleshooting

### Fix Takes Too Long

**Symptoms:**
- Fixes consistently timeout
- Takes more than 3 minutes for cold start

**Solutions:**
1. Verify antenna connection
2. Check if time sync is working (should see "Fetched time from LTE network")
3. Try increasing timeout in womo_gps.cpp
4. Ensure clear view of sky

### LTE Doesn't Re-enable

**Symptoms:**
- LTE stays disabled after GPS fix
- No network connectivity after fix

**Solutions:**
1. Check LTE task is running
2. Verify `s_lte_command_queue` is created
3. Check logs for "Failed to re-enable LTE"
4. Ensure LTE was enabled before GPS fix started

### No GPS Data in Telemetry

**Symptoms:**
- GPS telemetry section missing from RS485 output

**Solutions:**
1. Check `WALTER_ENABLE_GPS` is set to 1
2. Verify GPS task was created successfully
3. Check if any fixes have completed (`fix_count > 0`)
4. Look for GPS task startup in logs

### Frequent GPS Fixes Drain Power

**Symptoms:**
- Battery drains quickly
- System gets warm

**Solutions:**
1. Increase `WALTER_GPS_FIX_INTERVAL_MS` (e.g., to 2 hours: 7200000)
2. Consider conditional fixes (only when moving)
3. Reduce other sensor polling rates

## Performance Characteristics

### Timing

- **Cold Start**: 90-180 seconds (no previous fix, no time)
- **Hot Start**: 30-60 seconds (previous fix, valid time)
- **Time to First Fix (TTFF)**: Improved by LTE time sync

### Power Consumption

- **GNSS Active**: ~100-150 mA
- **LTE Active**: ~200-300 mA  
- **Both Idle**: ~20-30 mA

### Accuracy

- **Horizontal**: Typically 2-5 meters (with good signal)
- **Vertical**: Typically 5-10 meters
- **Speed**: ±0.1 km/h

## Integration Examples

### Custom Fix Trigger

```c
// In your code
#include "womo_gps.h"

void request_emergency_fix(void) {
    ESP_LOGI("app", "Requesting emergency GPS fix");
    esp_err_t err = womo_gps_execute_fix_cycle();
    if (err == ESP_OK) {
        womo_gps_data_t fix;
        if (womo_gps_get_last_fix(&fix) == ESP_OK) {
            // Send fix to emergency service
            send_emergency_location(fix.latitude, fix.longitude);
        }
    }
}
```

### Geofencing

```c
bool check_in_allowed_area(void) {
    if (!womo_gps_is_valid()) {
        return false; // No fix yet
    }
    
    womo_gps_data_t fix;
    womo_gps_get_last_fix(&fix);
    
    // Check if within geofence
    return is_within_bounds(
        fix.latitude, fix.longitude,
        FENCE_LAT_MIN, FENCE_LAT_MAX,
        FENCE_LON_MIN, FENCE_LON_MAX
    );
}
```

## Future Enhancements

Potential improvements for future versions:

1. **Adaptive Interval**: Adjust fix frequency based on movement
2. **Geofence Alerts**: Trigger events when leaving/entering areas  
3. **Track Logging**: Store position history to NVS
4. **Motion Detection**: Skip fixes when vehicle stationary
5. **AGNSS Support**: Faster fixes with assisted GPS data
6. **Power Profiles**: Different modes for battery vs. powered operation

## References

- [Walter Modem Documentation](../walter-esp-idf/)
- [Sequans Monarch 2 GNSS AT Commands](https://www.sequans.com/)
- [Hardware Documentation](hardware_walter.md)
