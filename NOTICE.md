# Notices

System Audio Route is licensed under GNU GPL version 3. See `LICENSE`.

## Third-party components

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
