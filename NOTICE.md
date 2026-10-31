# Notices

System Audio Route is licensed under GNU GPL version 3. See `LICENSE`.

## Third-party components

- **VB-CABLE Driver Pack 45** (proprietary donationware by VB-Audio / Vincent
  Burel), optionally bundled unmodified under the published VB-CABLE distribution
  conditions at https://vb-audio.com/Services/licensing.htm. Origin:
  https://www.vb-cable.com/ . VB-CABLE is donationware; all contributions are
  welcome. Original files, license/readme and ZIP are retained under `vendor/`.
  It is a separate third-party driver, not GPL SAR code. It is installed only
  through its own vendor setup after user approval and is not removed by SAR.
  This permission does not cover bundling VB-CABLE A/B or C/D. Hi-Fi Cable and
  additional cables are not included. Professional deployments must check the
  vendor's applicable licensing requirements.

- **nlohmann/json 3.11.3** (MIT), used only by the optional VSTGUI preview to
  decode the independent virtual-device manager's JSON inventory. Its license
  ships with that preview in `licenses/nlohmann-json/LICENSE.MIT`.
- **VSTGUI 4.9** (BSD-3-Clause), used only by the optional native control-panel
  preview. Its license ships with that preview in `licenses/vstgui/LICENSE`.
- **Steinberg ASIO SDK 2.3.4** (unmodified interface headers). Copyright
  Steinberg Media Technologies GmbH, used under the GPLv3 option described by
  the SDK's included license. Source and archive verification details are in
  `third_party/asio_sdk_2.3.4/README.md`; the license text ships in
  `licenses/asio-sdk/`. ASIO is a trademark and software of Steinberg Media
  Technologies GmbH. System Audio Route is not affiliated with or endorsed by
  Steinberg. Steinberg logo artwork is not included.
- **libsamplerate 0.2.2** (BSD-2-Clause), statically linked for sample-rate
  conversion. Copyright Erik de Castro Lopo and contributors. The license text
  ships in `licenses/libsamplerate/COPYING`.
- **Qt 6** (Qt Base and Qt Declarative), dynamically linked by the control
  application and shipped as unmodified shared libraries under the GNU LGPL
  version 3. Qt is a trademark of The Qt Company Ltd. The Qt license texts and
  the corresponding source are available from <https://www.qt.io/licensing/>
  and <https://download.qt.io/>. You may replace the shipped Qt libraries with
  your own build of the same Qt version.
- **Microsoft Visual C++ Redistributable (x64)**, redistributed unmodified
  under the Microsoft Visual Studio redistribution terms and installed by the
  installer when the runtime is missing.
