# gpiokit.bi -- gpiokit for Onyx BASIC (#import gpiokit): made by tools/kitbi/kitbi.py from gpiokit.abi and the kit's headers;
# not edited by hand. <name> <place> <result> <arguments or -> <C name>; the types: user/Libs/basic/basint.h.
# struct <name> <size> <C name>, then its fields: field <name> <offset> <kind> [<length> | <structure>].
kit gpiokit 34
struct event 16 gk_event
field pin 0 b
field edge 1 b
field level 2 b
field lost 3 b
field reserved 4 u
field us 8 l
struct pin_info 40 gk_pin_info
field pin 0 b
field mode 1 b
field level 2 b
field flags 3 b
field owner 4 u
field pwm_freq 8 u
field pwm_duty 12 u
field reason 16 a 24
available 0 i - gk_available
edges 1 i ii gk_edges pin,edges
error 2 s i gk_error code
events 3 i pii gk_events out,max,wait_ms
gpio_function 4 s i gk_gpio_function gpio
header_gpio 5 i i gk_header_gpio header_pin
header_label 6 s i gk_header_label header_pin
header_pin 7 i i gk_header_pin gpio
i2c_close 8 i - gk_i2c_close
i2c_guess 9 s i gk_i2c_guess addr
i2c_open 10 i i gk_i2c_open clock_hz
i2c_read 11 i ipi gk_i2c_read addr,data,n
i2c_reg_read 12 i ii gk_i2c_reg_read addr,reg
i2c_reg_write 13 i iii gk_i2c_reg_write addr,reg,value
i2c_scan 14 i p gk_i2c_scan map
i2c_write 15 i ipi gk_i2c_write addr,data,n
i2c_write_read 16 i ipipi gk_i2c_write_read addr,wr,wn,rd,rn
info 17 i pi gk_info out,max
mode 18 i ii gk_mode pin,mode
mode_name 19 s i gk_mode_name mode
now_us 20 l - gk_now_us
pwm 21 i iii gk_pwm pin,freq_hz,duty
read 22 i i gk_read pin
read_all 23 u - gk_read_all
release 24 i - gk_release
servo 25 i ii gk_servo pin,pulse_us
sim 26 i i gk_sim on
sim_display 27 i p gk_sim_display out
sim_input 28 i ii gk_sim_input pin,level
spi_close 29 i - gk_spi_close
spi_open 30 i ii gk_spi_open clock_hz,mode
spi_transfer 31 i ippi gk_spi_transfer cs,tx,rx,n
toggle 32 i i gk_toggle pin
write 33 i ii gk_write pin,level
