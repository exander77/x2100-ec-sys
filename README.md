# x2100-ec-sys

```
dkms add -m x2100-ec-sys/1.0
dkms build -m x2100-ec-sys/1.0
dkms install -m x2100-ec-sys/1.0
```

Write support:

```
modprobe -r x2100_ec_sys
modprobe x2100_ec_sys write_support=1
```
