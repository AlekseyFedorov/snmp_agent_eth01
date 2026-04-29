# SNMP OID Map

Base enterprise OID: `.1.3.6.1.4.1.9999.1`

Read community: `public` by default.
Write community: `private` by default.

Both values can be overridden at build time with the `SNMP_READ_COMMUNITY` and
`SNMP_WRITE_COMMUNITY` CMake cache variables.

## Read-only OIDs

| OID suffix | Full OID | Type | Meaning |
| --- | --- | --- | --- |
| `.1.0` | `.1.3.6.1.4.1.9999.1.1.0` | INTEGER | DS18B20 temperature multiplied by 10 |
| `.2.0` | `.1.3.6.1.4.1.9999.1.2.0` | Gauge32 | Free heap bytes |
| `.3.0` | `.1.3.6.1.4.1.9999.1.3.0` | TimeTicks | Device uptime |
| `.4.0` | `.1.3.6.1.4.1.9999.1.4.0` | Gauge32 | Used heap percent |
| `.5.0` | `.1.3.6.1.4.1.9999.1.5.0` | OCTET STRING | Ethernet MAC address |
| `.6.0` | `.1.3.6.1.4.1.9999.1.6.0` | INTEGER | Water leak status, `0` dry, `1` leak |
| `.7.0` | `.1.3.6.1.4.1.9999.1.7.0` | INTEGER | Door sensor 1 status, `0` closed, `1` open |
| `.8.0` | `.1.3.6.1.4.1.9999.1.8.0` | INTEGER | Door sensor 2 status, `0` closed, `1` open |

## Read-write OIDs

| OID suffix | Full OID | Type | Meaning |
| --- | --- | --- | --- |
| `.10.0` | `.1.3.6.1.4.1.9999.1.10.0` | OCTET STRING | Static IP address |
| `.11.0` | `.1.3.6.1.4.1.9999.1.11.0` | OCTET STRING | Gateway address |
| `.12.0` | `.1.3.6.1.4.1.9999.1.12.0` | OCTET STRING | Netmask |

Network values are validated as dotted IPv4 strings before being written to NVS.
Netmask values must be contiguous masks such as `255.255.255.0`.
After a successful write, the device schedules a reboot after one second.

## Examples

```bash
idf.py -DSNMP_WRITE_COMMUNITY=my-write-secret build
```

```bash
snmpget -v 2c -c public 192.168.2.50 .1.3.6.1.4.1.9999.1.7.0
snmpget -v 2c -c public 192.168.2.50 .1.3.6.1.4.1.9999.1.8.0
snmpwalk -v 2c -c public 192.168.2.50 .1.3.6.1.4.1.9999.1
```

```bash
snmpset -v 2c -c private 192.168.2.50 \
  .1.3.6.1.4.1.9999.1.10.0 s "192.168.2.55" \
  .1.3.6.1.4.1.9999.1.11.0 s "192.168.2.1" \
  .1.3.6.1.4.1.9999.1.12.0 s "255.255.255.0"
```
