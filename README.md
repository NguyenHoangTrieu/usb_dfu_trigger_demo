# USB-HS DFU Trigger Demo (ESP32-P4)

Nạp firmware qua cổng **USB-A High Speed** (USB_DM/DP, USB-OTG 2.0 của chip) mà
không cần bấm BOOT/RST: thiết bị lộ ra một cổng USB-CDC; gửi chuỗi `"DFU"` vào
cổng đó sẽ khiến chip tự đặt bit force-download-boot và tự restart vào chế độ
download, sẵn sàng cho `idf.py dfu-flash`.

**Đã kiểm chứng end-to-end trên phần cứng thật** (Waveshare
ESP32-P4-WIFI6-DEV-KIT, chip rev v1.0) — xem mục "Trạng thái đã kiểm chứng"
bên dưới. ESP-IDF: v6.0.2 (local). Target: `esp32p4`.

## Cách build

```bash
source $IDF_PATH/export.sh
cd usb_dfu_trigger_demo
idf.py set-target esp32p4   # đã chạy sẵn, build/ đã có trong repo này
idf.py build
```

Nạp lần đầu vẫn cần cách thông thường (USB CH343/UART hoặc BOOT+RST), vì
chưa có firmware chạy sẵn để nhận trigger "DFU":

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

## ⚠️ Việc board này cần làm TRƯỚC — sửa phần cứng (bắt buộc, 1 lần)

Trên board Waveshare ESP32-P4-WIFI6-DEV-KIT trong tay, cổng USB-A thứ 2
(gần SPK/MIC, net `USBHSD1_P/N`) là cổng Device thật, nhưng chip chuyển mạch
**FSUSB42UMX (U15)** quyết định tín hiệu HS của chip đi ra cổng nào, và chân
`SEL` của nó **không có jumper 3 chân chọn được cả 2 phía** như silkscreen
"HOST"/"Device" gợi ý — xem mục "Phát hiện phần cứng" bên dưới để biết lý do.
Phải **sửa tay**:

1. Gỡ điện trở `R36` (0Ω, cạnh header `H3`) — nó kéo cứng `SEL` xuống GND.
2. Câu 1 dây từ chân `SEL` (pin 2 của `H3`, hoặc thẳng chân 10 của `U15`) lên
   `3.3V` (ví dụ chân VCC của chính U15, hoặc bất kỳ điểm `ESP_3V3` nào gần
   đó), tốt nhất qua điện trở nhỏ (~10kΩ) cho an toàn.

**Phải gỡ `R36` trước khi câu dây lên 3.3V** — nếu câu dây mà chưa gỡ `R36`,
sẽ tạo đoản mạch trực tiếp 3.3V↔GND qua `R36` (đã xảy ra thật trong lúc debug
project này; board không hỏng nhưng rất rủi ro, xem lịch sử hội thoại).

Cắm cáp dữ liệu (không phải cáp chỉ-sạc) vào cổng USB-A **gần SPK/MIC**
(J8) — không phải cổng gần Ethernet (J2, đó là cổng Host/qua hub, không dùng
được cho tính năng này).

## Linux: udev rule cho dfu-util (1 lần)

`dfu-util`/`idf.py dfu-flash` cần quyền truy cập USB trực tiếp. Tạo
`/etc/udev/rules.d/40-dfuse.rules`:

```
SUBSYSTEMS=="usb", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="00??", GROUP="plugdev", MODE="0666"
```

Rồi `sudo udevadm control --reload-rules && sudo udevadm trigger` (hoặc khởi
động lại máy). User cần thuộc group `plugdev` (hoặc group tương đương, kiểm
tra bằng `groups`).

## Cách chạy (flash các lần sau, không bấm nút)

```bash
./flash_dfu.sh --cdc-port /dev/ttyACM1
```

Script làm 4 bước: build `dfu.bin` (`idf.py dfu`) → gửi `"DFU"` tới
`--cdc-port` → chờ thiết bị DFU `303a:0012` xuất hiện (tối đa `--timeout`
giây, mặc định 15s) → `idf.py dfu-flash`. Tham số:

- `--cdc-port /dev/ttyACMx` (bắt buộc): cổng serial CDC mà firmware lộ ra
  trên cổng USB-HS (J8). Tìm bằng `ls /dev/serial/by-id/` sau khi cắm cáp —
  CDC này **khác** cổng UART/CH343 dùng để flash lần đầu (ví dụ
  `/dev/ttyACM0` = CH343, `/dev/ttyACM1` = CDC app này).
- `--dfu-path BUS-PORT` (tuỳ chọn): truyền qua `idf.py dfu-flash --path`, cần
  khi có nhiều board 303a cùng cắm (dùng `idf.py dfu-list` để biết path).
- `--timeout SECONDS` (tuỳ chọn, mặc định 15).

## Cách hoạt động (và nguồn tham chiếu cho từng phần)

| Thành phần | Tên chính xác | Nguồn |
|---|---|---|
| Bit force-download-boot | `LP_SYS.sys_ctrl.force_download_boot` (bit 2 của `LP_SYSTEM_REG_SYS_CTRL_REG`, macro `LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT`) | `esp-idf/components/soc/esp32p4/register/hw_ver3/soc/lp_system_struct.h:59-62`, `lp_system_reg.h:63-69` — field giống hệt ở `hw_ver1` (chip rev v1.0 dùng hw_ver1) |
| eFuse khóa chức năng trên | `ESP_EFUSE_DIS_FORCE_DOWNLOAD` (EFUSE_BLK0, bit 44) | `esp-idf/components/efuse/esp32p4/esp_efuse_table.csv:175`, `esp_efuse_table_v3.0.h:213` |
| Cổng USB HS native (OTG 2.0) | `tinyusb_config_t.port = TINYUSB_PORT_HIGH_SPEED_0` | `managed_components/espressif__esp_tinyusb/include/tinyusb.h` (enum `tinyusb_port_t`), và `tinyusb.c:285-296` (tự cấu hình PHY `USB_PHY_SPEED_HIGH` khi chọn port này) |
| TinyUSB có hỗ trợ HS trên P4 | `CFG_TUD_MAX_SPEED = OPT_MODE_HIGH_SPEED` khi `CONFIG_IDF_TARGET_ESP32P4` | `managed_components/espressif__esp_tinyusb/include/tusb_config.h:92-97` |
| VID:PID thiết bị DFU khi vào download mode | `303a:0012` | `esp-idf/tools/cmake/dfu.cmake:26-27` (`esp32p4` → `dfu_pid="12"`) — **đã xác nhận trên phần cứng thật**, `lsusb` thấy đúng `303a:0012 Espressif ESP32-P4` sau khi gửi "DFU" |
| `idf.py dfu` / `dfu-list` / `dfu-flash` hỗ trợ esp32p4 | `SUPPORTED_TARGETS = ['esp32s2', 'esp32s3', 'esp32p4']` | `esp-idf/tools/idf_py_actions/dfu_ext.py:15` |
| Giới hạn Secure Boot/Flash Encryption chặn USB-OTG download | "enabling Secure Boot or flash encryption disables the USB-OTG USB stack in the ROM, disallowing updates via the serial emulation or DFU on that port" | `esp-idf/docs/en/api-guides/dfu.rst` |

Firmware (`main/main.c`):
1. Khởi động TinyUSB CDC trên `TINYUSB_PORT_HIGH_SPEED_0`.
2. Callback RX dùng sliding-window 3 byte để phát hiện chuỗi `"DFU"` bất kể
   host chia gói thế nào.
3. Khi khớp: gửi ack `"DFU-OK\r\n"`, flush, delay 50ms, set
   `LP_SYS.sys_ctrl.force_download_boot = 1`, gọi `esp_restart()`.
4. Trước đó, lúc boot, đọc `ESP_EFUSE_DIS_FORCE_DOWNLOAD` và log cảnh báo nếu
   fuse này đã bị khóa (=1).

`sdkconfig.defaults` còn tắt 2 thứ cần thiết để TinyUSB chiếm được PHY:
`CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` và `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n`
— xem "Phát hiện phần cứng/phần mềm" bên dưới.

## Trạng thái đã kiểm chứng

Toàn bộ chuỗi đã chạy thật trên board (không phải chỉ build/log):

1. `./flash_dfu.sh --cdc-port /dev/ttyACM1` → gửi "DFU" → board tự restart.
2. `lsusb` thấy `303a:0012 Espressif ESP32-P4` (đúng VID:PID tính toán từ
   `dfu.cmake`, không phải đoán).
3. `idf.py dfu-flash` chạy, `dfu-util` tải 256000 byte, kết thúc
   `Download done... Flash successful.`

## Phát hiện phần cứng/phần mềm trong lúc debug (quan trọng, không hiển nhiên)

- **USB_SERIAL_JTAG chiếm PHY mặc định.** ESP-IDF mặc định bật
  `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` và
  `CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y` (độc lập với console chính). Việc
  này khiến host luôn thấy `303a:1001 "Espressif USB JTAG/serial debug
  unit"` thay vì CDC app thật. Phải tắt cả hai (xem `sdkconfig.defaults`).
  Phát hiện này đến từ việc thử cổng OTG1.1 Full-Speed (H2) làm đối chứng —
  khi tắt USB_SERIAL_JTAG, thiết bị `1001` biến mất hẳn, xác nhận cổng H2
  trên board này thực chất là cổng USB_SERIAL_JTAG, **không phải** OTG1.1
  độc lập.
- **Switch FSUSB42UMX (U15) định tuyến tín hiệu HS, và jumper H3 không đủ
  để chọn "Device".** Chân `SEL` của U15 (`SEL:H-->ngõ2(J8/Device);
  L-->ngõ1(J2/Host qua hub CH334F)`) nối tới header `H3`, nhưng theo
  schematic, `H3` pin 3 **không nối đi đâu (NC)** — chỉ có pin 1 (qua `R36`,
  0Ω, xuống GND) và pin 2 (nối `SEL`) là có tác dụng thật. Không có vị trí
  jumper nào kéo `SEL` lên 3.3V, dù silkscreen board có in "HOST"/"Device"
  cạnh header này. Phải sửa tay (xem mục ở trên) để thực sự chọn Device.
- **Chưa xác minh được**: đây có phải lỗi thiết kế của riêng board revision
  này, hay là chủ đích (ví dụ: dự định một board riêng/rework note khác mà
  tài liệu đầy đủ của Waveshare có thể giải thích, nhưng tôi không có file
  đó). Chỉ dựa trên 3 trang schematic trích xuất sẵn có.

## Hạn chế / Chưa kiểm chứng

- **[Đã kiểm chứng bằng tài liệu chính thức]** Secure Boot hoặc Flash
  Encryption bật sẽ tắt toàn bộ USB-OTG stack trong ROM — cơ chế trigger này
  **sẽ không hoạt động** trên chip đã bật 1 trong 2 tính năng đó. Nguồn:
  `esp-idf/docs/en/api-guides/dfu.rst`.
- **[Chưa kiểm chứng]** Hành vi chính xác của ROM khi đọc bit
  `force_download_boot` ngoài những gì quan sát được qua `lsusb` (ví dụ nó
  có hoàn toàn bỏ qua trạng thái chân strapping BOOT/RST trong mọi trường
  hợp hay không — thực tế test ở đây luôn ở trạng thái BOOT/RST thả nổi
  bình thường, chưa thử giữ BOOT trong lúc trigger). Thuật ngữ "Joint
  Download Boot" là do người dùng cung cấp; tài liệu ROM boot-mode-select
  đầy đủ không nằm trong mã nguồn ESP-IDF.
- **Firmware treo/crash**: nếu firmware bị treo trước khi vào task xử lý
  CDC RX (hoặc crash loop), nó sẽ không bao giờ nhận được "DFU" — vẫn phải
  bấm giữ BOOT, tap RST, rồi `idf.py dfu-flash` thủ công như bình thường.
- Script `flash_dfu.sh` lọc thiết bị theo chuỗi `303a:0012` trong output của
  `idf.py dfu-list`; nếu Espressif đổi cách gán `dfu_pid` cho esp32p4 trong
  một phiên bản ESP-IDF khác, chuỗi này cần cập nhật lại theo
  `tools/cmake/dfu.cmake`.
- Cổng CDC dùng để gửi "DFU" là cổng **do chính firmware này tạo ra trên
  cổng USB-HS (J8)**. Sau khi nhận "DFU" và restart, cổng CDC này biến mất
  (chip re-enumerate thành thiết bị DFU `303a:0012`), đây là hành vi mong
  đợi, không phải lỗi.
- Nếu board bị ngắt khỏi download mode giữa chừng (ví dụ do
  `udevadm trigger` hoặc rút/cắm cáp), chỉ cần gửi lại "DFU" — không cần
  bấm nút gì, chạy lại `flash_dfu.sh` là đủ.
