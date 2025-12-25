# FAQ

## Does this work without WiFi?

Yes. Network features are optional and can be disabled in `include/Config.h`.

## Do I need an MPU6050 breakout?

Yes. The firmware expects an MPU6050 over I2C.

## Can I tune sensitivity?

Yes. Adjust the warning/critical threshold multipliers in `include/Config.h`.
