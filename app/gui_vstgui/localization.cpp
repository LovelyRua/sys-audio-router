#include "app/gui_vstgui/localization.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <limits>
#include <mutex>
#include <string_view>

namespace sar::gui_vstgui {
namespace {
constexpr wchar_t kKey[] = L"Software\\System Audio Route\\System Audio Route";
constexpr wchar_t kValue[] = L"language";
std::once_flag initialized;
std::atomic<bool> chinese{false};

bool parse(std::wstring_view value, UiLanguage& language) {
  if (value == L"system") language = UiLanguage::System;
  else if (value == L"en") language = UiLanguage::English;
  else if (value == L"zh_CN") language = UiLanguage::Chinese;
  else return false;
  return true;
}

struct Translation { const char* english; const char* chinese; };
// UTF-8 literals; callers localize fixed fragments before composing dynamic text.
constexpr Translation catalog[] = {
    {"MUTE", "静音"}, {"ON", "开启"},
    {"Engine offline", "引擎离线"}, {"Action failed", "操作失败"},
    {"Not configured", "未配置"}, {"Audio running", "音频运行中"},
    {"Audio stopped", "音频已停止"}, {"Configure audio", "配置音频"},
    {"Stop audio", "停止音频"}, {"Start audio", "启动音频"},
    {"Engine request failed", "引擎请求失败"},
    {"Too many pending route edits; wait for the engine", "待处理路由修改过多，请等待引擎响应"},
    {"Could not start the engine request worker", "无法启动引擎请求工作线程"},
    {"No audio runtime configured", "尚未配置音频运行环境"},
    {"Matrix unavailable", "矩阵不可用"},
    {" inputs  /  ", " 输入 / "}, {" outputs  /  ", " 输出 / "},
    {" routes", " 路由"}, {"  /  applying", " / 正在应用"},
    {"No source selected", "未选择源"}, {"No destination selected", "未选择目标"},
    {"Unmute", "取消静音"}, {"Mute", "静音"},
    {"WORKSPACE", "工作区"}, {"Routing matrix", "路由矩阵"},
    {"Audio devices", "音频设备"}, {"Audio devices and VB-CABLE setup", "音频设备与 VB-CABLE 安装"},
    {"Virtual ASIO", "虚拟 ASIO"},
    {"Virtual ASIO names, channels and enabled states", "虚拟 ASIO 名称、通道与启用状态"},
    {"Presets", "预设"}, {"Save, load and manage routing presets", "保存、加载和管理路由预设"},
    {"Diagnostics", "诊断"}, {"Engine counters and connection status", "引擎计数器与连接状态"},
    {"EXPERIMENTAL", "实验功能"}, {"Driver lab", "驱动实验室"},
    {"Experimental virtual driver management", "实验性虚拟驱动管理"},
    {"Native Preview", "原生预览版"}, {"ROUTING MATRIX", "路由矩阵"},
    {"SOURCE / DESTINATION", "源 / 目标"}, {"ROUTE INSPECTOR", "路由检查器"},
    {"TO", "至"}, {"GAIN", "增益"}, {"Remove route", "移除路由"},
    {"Rows <", "行 <"}, {"Rows >", "行 >"}, {"Cols <", "列 <"}, {"Cols >", "列 >"},
    {"Click: enable/mute. Shift-click: connect/remove. Right-click: inspect.", "单击：启用/静音。Shift 单击：连接/移除。右键：检查。"},
    {"Route gain: -60 to +12 dB. Double-click: 0 dB.", "路由增益：-60 至 +12 dB。双击：0 dB。"},
    {"AUDIO DEVICES", "音频设备"}, {"Refresh", "刷新"},
    {"Reading runtime...", "正在读取运行环境..."}, {"Matrix endpoint", "矩阵端点"},
    {"+ Capture", "+ 采集"}, {"+ Playback", "+ 播放"}, {"+ ASIO", "+ ASIO"},
    {"Remove endpoint", "移除端点"}, {"Reload saved", "重新加载已保存配置"},
    {"Endpoint ID", "端点 ID"}, {"Set clock master", "设为主时钟"},
    {"Audio device", "音频设备"}, {"First channel", "起始通道"},
    {"Channel count", "通道数"}, {"ASIO rate (Hz)", "ASIO 采样率 (Hz)"},
    {"Buffer (frames)", "缓冲区（帧）"}, {"New matrix", "新建矩阵"}, {"Apply", "应用"},
    {"Install VB-CABLE...", "安装 VB-CABLE..."}, {"VB-Audio / Donate", "VB-Audio / 捐赠"},
    {"VB-CABLE by VB-Audio (vb-cable.com). Donationware; contributions are welcome.", "VB-CABLE 由 VB-Audio (vb-cable.com) 提供，欢迎捐赠支持。"},
    {"Open the original VB-Audio installer? VB-CABLE is donationware from vb-cable.com. Review its terms and complete installation yourself. A reboot may be required. Existing cable routes may be interrupted.", "打开 VB-Audio 原版安装程序？VB-CABLE 是来自 vb-cable.com 的捐赠软件。请阅读条款并自行完成安装，可能需要重启。现有虚拟线缆路由可能中断。"},
    {"Opening original VB-CABLE setup...", "正在打开 VB-CABLE 原版安装程序..."},
    {"Could not start setup worker", "无法启动安装工作线程"},
    {"Discard unsaved endpoint changes and reload the engine configuration?", "放弃未保存的端点修改并重新加载引擎配置？"},
    {"Create a new empty WASAPI matrix draft? Existing configuration is not changed until Apply is requested.", "创建空的 WASAPI 矩阵草稿？点击应用前不会更改现有配置。"},
    {"Remove both Physical ASIO endpoints from this draft? Routes using their channels may be removed after Apply. Remaining WASAPI endpoints are retained.", "从草稿中移除两个物理 ASIO 端点？应用后，使用其通道的路由可能被移除。其余 WASAPI 端点将保留。"},
    {"Apply this matrix configuration? The ASIO driver may reopen and audio may be interrupted. Removed channels can lose routes. Hardware must support the requested channel ranges, sample rate and buffer size.", "应用此矩阵配置？ASIO 驱动可能重新打开，音频可能中断。被移除通道的路由可能丢失。硬件必须支持所请求的通道范围、采样率和缓冲区大小。"},
    {"Applying runtime configuration...", "正在应用运行配置..."},
    {"Stopping audio...", "正在停止音频..."}, {"Starting audio...", "正在启动音频..."},
    {"Could not start runtime request worker", "无法启动运行环境请求工作线程"},
    {"Reading engine configuration and device inventory...", "正在读取引擎配置和设备列表..."},
    {"Vendor setup request failed. No automatic retry.", "厂商安装请求失败，未自动重试。"},
    {"Runtime request failed; draft retained. Refresh to read actual state.", "运行环境请求失败，草稿已保留。请刷新以读取实际状态。"},
    {" / capture", " / 采集"}, {" / playback", " / 播放"}, {" / MASTER", " / 主时钟"},
    {"No configured endpoints", "没有已配置的端点"}, {" / default", " / 默认"},
    {"Configured binding unavailable / preserved", "已配置的绑定不可用 / 已保留"},
    {"Select a device", "选择设备"},
    {"ASIO group / master clock / native channel offset is zero-based", "ASIO 组 / 主时钟 / 原生通道偏移从零开始"},
    {"Native channel offset is zero-based", "原生通道偏移从零开始"},
    {"Remove ASIO group", "移除 ASIO 组"}, {"Clock master", "主时钟"},
    {"Ready to apply", "可以应用"}, {"Engine unavailable", "引擎不可用"},
    {"Existing runtime preserved / read-only", "已保留现有运行环境 / 只读"},
    {" endpoints", " 个端点"}, {" / Unsaved changes", " / 有未保存的修改"},
    {"Virtual ASIO devices", "虚拟 ASIO 设备"}, {"VIRTUAL ASIO DEVICES", "虚拟 ASIO 设备"},
    {"Reading engine...", "正在读取引擎..."}, {"+ Device", "+ 设备"}, {"Remove", "移除"},
    {"Device name", "设备名称"}, {"DAW inputs", "DAW 输入"}, {"DAW outputs", "DAW 输出"},
    {"Enabled", "启用"}, {"Apply and reload", "应用并重新加载"},
    {"Discard unsaved Virtual ASIO edits and read the current engine topology?", "放弃未保存的虚拟 ASIO 修改并读取当前引擎拓扑？"},
    {"Could not generate a device identity.", "无法生成设备标识。"},
    {"Device limit reached or generated identity was rejected.", "已达到设备上限或生成的标识被拒绝。"},
    {"Remove this device from the draft? It remains registered until Apply. Routes using removed channels may be lost after applying.", "从草稿中移除此设备？应用前仍保留注册。应用后，使用被移除通道的路由可能丢失。"},
    {"Apply the Virtual ASIO names, enabled states and channel counts? All DAWs must be closed. The stopped engine will reload its current session; routes using removed channels may be removed.", "应用虚拟 ASIO 名称、启用状态和通道数？必须关闭所有 DAW。已停止的引擎将重新加载当前会话，使用被移除通道的路由可能被移除。"},
    {"Applying Virtual ASIO topology...", "正在应用虚拟 ASIO 拓扑..."},
    {" [disabled]", " [已禁用]"}, {"No topology available", "没有可用拓扑"},
    {"Enabled totals: ", "已启用总计："}, {" DAW inputs / ", " DAW 输入 / "},
    {" DAW outputs", " DAW 输出"}, {"Stop audio before applying", "应用前请停止音频"},
    {"Close DAW clients before applying", "应用前请关闭 DAW 客户端"},
    {" devices", " 个设备"}, {" / Reloading", " / 正在重新加载"},
    {"Could not start ASIO request worker.", "无法启动 ASIO 请求工作线程。"},
    {"ASIO request failed; refresh before retrying.", "ASIO 请求失败，请刷新后再重试。"},
    {"Topology saved; waiting for the engine to reload the same session...", "拓扑已保存，正在等待引擎重新加载同一会话..."},
    {"Topology reply did not match the draft; refresh required.", "拓扑响应与草稿不匹配，需要刷新。"},
    {"Virtual ASIO topology reloaded and verified.", "虚拟 ASIO 拓扑已重新加载并验证。"},
    {"Engine topology changed elsewhere. Reload saved before applying this draft.", "引擎拓扑已在其他位置更改。请先重新加载已保存配置，再应用此草稿。"},
    {"Topology was saved but reload could not be verified. Refresh; no command was resent.", "拓扑已保存，但无法验证重新加载结果。请刷新；未重新发送命令。"},
    {"PRESETS", "预设"}, {"Saved presets", "已保存的预设"}, {"Load", "加载"},
    {"Delete", "删除"}, {"Preset name", "预设名称"}, {"Save current", "保存当前配置"},
    {"Reading presets...", "正在读取预设..."}, {"Working...", "正在处理..."},
    {"Replace the current engine routing with the selected preset?", "用所选预设替换当前引擎路由？"},
    {"Permanently delete the selected preset file?", "永久删除所选预设文件？"},
    {"Could not start preset worker.", "无法启动预设工作线程。"},
    {"Preset worker failed. No automatic retry was made.", "预设工作线程失败，未自动重试。"},
    {"No saved presets", "没有已保存的预设"},
    {"A preset with this name already exists. Replace its file?", "同名预设已存在。替换其文件？"},
    {"DIAGNOSTICS", "诊断"}, {"Unknown", "未知"}, {"Counter source", "计数器来源"},
    {"Graph version", "图版本"}, {"Processed blocks", "已处理块数"}, {"Xruns", "缓冲异常次数"},
    {"Capture FIFO fill (frames)", "采集 FIFO 填充（帧）"}, {"Render FIFO fill (frames)", "播放 FIFO 填充（帧）"},
    {"Capture overflow (cycles)", "采集溢出（周期）"}, {"Capture overflow (frames)", "采集溢出（帧）"},
    {"Render overflow (cycles)", "播放溢出（周期）"}, {"Render overflow (frames)", "播放溢出（帧）"},
    {"Render underflow (cycles)", "播放欠载（周期）"}, {"Render underflow (frames)", "播放欠载（帧）"},
    {"Import conversion failures", "导入转换失败次数"}, {"Export conversion failures", "导出转换失败次数"},
    {"Virtual ASIO pushed blocks", "虚拟 ASIO 推送块数"}, {"Virtual ASIO dropped blocks", "虚拟 ASIO 丢弃块数"},
    {"Virtual ASIO underflows", "虚拟 ASIO 欠载次数"}, {"Virtual ASIO overflows", "虚拟 ASIO 溢出次数"},
    {"Virtual ASIO consumed blocks", "虚拟 ASIO 消费块数"}, {"Virtual ASIO mixed blocks", "虚拟 ASIO 混音块数"},
    {"Virtual ASIO silent reads", "虚拟 ASIO 静音读取次数"}, {"Virtual ASIO clipped samples", "虚拟 ASIO 削波采样数"},
    {"Virtual ASIO non-finite samples", "虚拟 ASIO 非有限采样数"}, {"Virtual ASIO max queue depth", "虚拟 ASIO 最大队列深度"},
    {"Virtual ASIO active producers", "虚拟 ASIO 活跃生产者数"},
    {"Recovery episodes", "恢复事件数"}, {"Successful recoveries", "成功恢复次数"}, {"Failed recoveries", "恢复失败次数"},
    {"Last recovery (ms)", "上次恢复（毫秒）"}, {"Maximum recovery (ms)", "最长恢复（毫秒）"},
    {"Notification reopens", "通知触发重开次数"}, {"Notification reset failures", "通知重置失败次数"},
    {"Wait timeout cycles", "等待超时周期数"}, {"Capture discontinuity cycles", "采集不连续周期数"},
    {"Recovery underflow (frames)", "恢复欠载（帧）"}, {"Max recovery silence (frames)", "最长恢复静音（帧）"},
    {"Max rate-clamped frames", "最大连续速率限制帧数"},
    {"Stopped", "已停止"}, {"Opening", "正在打开"}, {"Running", "运行中"},
    {"Quiescing", "正在静止"}, {"Backoff", "退避等待"}, {"Faulted", "故障"},
    {"Unknown / awaiting diagnostics", "未知 / 等待诊断"},
    {"Last callback (ms)", "上次回调（毫秒）"}, {"Peak callback (ms)", "峰值回调（毫秒）"},
    {"Virtual ASIO peak (linear)", "虚拟 ASIO 峰值（线性）"},
    {"Endpoint queue fill (frames)", "端点队列填充（帧）"}, {"Endpoint correction (ppm)", "端点校正 (ppm)"},
    {"Endpoint role", "端点角色"}, {"WASAPI RECOVERY", "WASAPI 恢复"},
    {"Reported totals, not interval deltas. Missing telemetry is Unknown; zero is not proof of audio continuity.", "显示累计总数，而非区间增量。缺失的遥测为未知；零值不能证明音频连续。"},
    {"Could not encode diagnostics request", "无法编码诊断请求"},
    {"Invalid or mismatched diagnostics response", "诊断响应无效或不匹配"},
    {"Diagnostics request rejected", "诊断请求被拒绝"},
    {"Engine replied without diagnostics; counters are unknown", "引擎响应不含诊断信息，计数器未知"},
    {"Could not start diagnostics worker", "无法启动诊断工作线程"}, {"Diagnostics worker failed", "诊断工作线程失败"},
    {"Snapshot age: ", "快照时间距今："}, {" s", " 秒"},
    {"No current counter snapshot", "没有当前计数器快照"},
    {" / Reading...", " / 正在读取..."}, {" / Polling every 2 s", " / 每 2 秒轮询"},
    {"Engine aggregate", "引擎汇总"}, {"Selected endpoint unavailable", "所选端点不可用"},
    {"Offline / engine unreachable; counters unknown", "离线 / 无法连接引擎，计数器未知"},
    {"Unknown / diagnostics unavailable", "未知 / 诊断不可用"},
    {"Warning / recorded underflows", "警告 / 已记录欠载"},
    {"Warning / recorded errors or recovery warning", "警告 / 已记录错误或恢复警告"},
    {"Counters received / recovery health unknown", "已收到计数器 / 恢复健康状态未知"},
    {"Counters received / no recorded counter warnings", "已收到计数器 / 无已记录的计数器警告"},
    {"Master", "主时钟"}, {"Follower", "从时钟"}, {"Not applicable", "不适用"},
    {"Unknown / recovery telemetry unavailable", "未知 / 恢复遥测不可用"},
    {" / Engine reports stopped", " / 引擎报告已停止"},
    {" / Engine reports healthy", " / 引擎报告健康"},
    {" / Engine reports degraded", " / 引擎报告降级"},
    {" / Engine reports faulted", " / 引擎报告故障"},
    {" / Engine health unknown", " / 引擎健康状态未知"}, {" / Reopen pending", " / 等待重新打开"},
    {"Reason: ", "原因："}, {"Not supplied", "未提供"}, {"Reason: Unknown", "原因：未知"},
    {"Recovery telemetry unavailable", "恢复遥测不可用"},
    {"Language", "语言"}, {"System", "跟随系统"}, {"English", "英语"},
    {"Chinese", "简体中文"}, {"Restart required", "需要重启"},
    {"System language", "跟随系统"}, {"Chinese (Simplified)", "简体中文"},
    {"Restart required to apply language", "重启应用后语言设置生效"},
    {"Restart System Audio Route to apply the new language.", "重启 System Audio Route 后应用新的语言设置。"},
    {"Could not save the language preference.", "无法保存语言设置。"},
    {"Channel offset must be 0-255; channel count must be 1-256", "起始通道须为 0-255；通道数须为 1-256"},
    {"Endpoint ID must contain 1-128 bytes without control characters", "端点 ID 须为 1-128 字节，不能包含控制字符"},
    {"Channel range must fit within 256 native channels", "原生通道范围不能超过 256 个通道"},
    {"ASIO sample rate must be 8000-768000 Hz; buffer must be 1-65536 frames", "ASIO 采样率须为 8000-768000 Hz；缓冲须为 1-65536 帧"},
    {"Channel counts must be whole numbers between 1 and 64.", "通道数须为 1 至 64 的整数。"},
};
}  // namespace

UiLanguage languagePreference() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    return UiLanguage::System;
  wchar_t value[16]{};
  DWORD type = 0, bytes = sizeof(value);
  const auto status = RegQueryValueExW(key, kValue, nullptr, &type,
      reinterpret_cast<BYTE*>(value), &bytes);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) ||
      bytes > sizeof(value) || bytes % sizeof(wchar_t) != 0)
    return UiLanguage::System;
  const auto count = bytes / sizeof(wchar_t);
  if (value[count - 1] != L'\0') return UiLanguage::System;
  UiLanguage language = UiLanguage::System;
  parse(std::wstring_view(value, count - 1), language);
  return language;
}

bool setLanguagePreference(UiLanguage language) {
  const wchar_t* value = nullptr;
  switch (language) {
    case UiLanguage::System: value = L"system"; break;
    case UiLanguage::English: value = L"en"; break;
    case UiLanguage::Chinese: value = L"zh_CN"; break;
    default: return false;
  }
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
  const auto status = RegSetValueExW(key, kValue, 0, REG_SZ,
      reinterpret_cast<const BYTE*>(value),
      static_cast<DWORD>((std::wcslen(value) + 1) * sizeof(wchar_t)));
  const auto closed = RegCloseKey(key);
  return status == ERROR_SUCCESS && closed == ERROR_SUCCESS;
}

void initializeLanguage() {
  std::call_once(initialized, [] {
    wchar_t overrideValue[16]{};
    const auto size = GetEnvironmentVariableW(L"SAR_NATIVE_UI_LANGUAGE", overrideValue, 16);
    UiLanguage language = UiLanguage::System;
    if (!size || size >= 16 || !parse(std::wstring_view(overrideValue, size), language))
      language = languagePreference();
    chinese.store(language == UiLanguage::Chinese ||
        (language == UiLanguage::System &&
         PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE));
  });
}

bool chineseLanguage() { return chinese.load(); }

const char* tr(const char* english) {
  if (!english || !chineseLanguage()) return english;
  for (const auto& entry : catalog)
    if (std::strcmp(english, entry.english) == 0) return entry.chinese;
  return english;
}

std::string trText(const std::string& english) {
  // Preserve embedded NULs rather than accidentally translating a prefix.
  if (english.find('\0') != std::string::npos) return english;
  return tr(english.c_str());
}

std::wstring trWide(const wchar_t* english) {
  if (!english) return {};
  if (!chineseLanguage()) return english;
  const auto length = std::wcslen(english);
  if (length == 0 || length > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    return english;
  const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, english,
      static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (!size) return english;
  std::string utf8(size, '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, english,
      static_cast<int>(length), utf8.data(), size, nullptr, nullptr)) return english;
  const auto translated = tr(utf8.c_str());
  if (translated == utf8.c_str()) return english;
  const auto wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, translated, -1, nullptr, 0);
  if (wideSize <= 0) return english;
  std::wstring wide(wideSize, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, translated, -1, wide.data(), wideSize))
    return english;
  wide.pop_back();
  return wide;
}

}  // namespace sar::gui_vstgui
