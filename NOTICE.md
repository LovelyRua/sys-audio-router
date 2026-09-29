# Notices

System Audio Route is licensed under GNU GPL version 3. See `LICENSE`.

## Third-party components

- **Microsoft SysVAD virtual audio driver sample**, forked from
  `microsoft/Windows-driver-samples` at commit
  `2dc3fd3a0cc84a2933f2194e7ec0871584979071`. Its source under
  `driver/windows_virtual_wasapi/sysvad/` remains subject to the Microsoft
  Public License; the full text is in `driver/windows_virtual_wasapi/MS-PL.txt`.
  The sample does not represent a finished SAR virtual audio driver.
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
