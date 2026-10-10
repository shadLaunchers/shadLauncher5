// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "sdk_metadata.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <utility>

namespace Core::Analysis {

namespace {
using enum AbiType;
}

const std::vector<FunctionSignature>& SeedSignatures() {
    static const std::vector<FunctionSignature> kSeeds = {
        {"sceKernelSleep", I32, {U32}},
        {"sceKernelUsleep", I32, {U32}},
        {"sceKernelGetProcessTime", U64, {}},
        {"sceKernelGetProcessTimeCounter", U64, {}},
        {"scePthreadCreate", I32, {Ptr, Ptr, Ptr, Ptr}},
        {"scePthreadJoin", I32, {Ptr, Ptr}},
        {"scePthreadMutexInit", I32, {Ptr, Ptr}},
        {"scePthreadMutexLock", I32, {Ptr}},
        {"scePthreadMutexUnlock", I32, {Ptr}},
        {"scePthreadCondInit", I32, {Ptr, Ptr}},
        {"scePthreadCondWait", I32, {Ptr, Ptr}},
        {"scePthreadCondSignal", I32, {Ptr}},
        {"malloc", Ptr, {U64}},
        {"free", Void, {Ptr}},
        {"memcpy", Ptr, {Ptr, Ptr, U64}},
        {"memset", Ptr, {Ptr, I32, U64}},
        {"printf", I32, {Ptr}, true},
        {"snprintf", I32, {Ptr, U64, Ptr}, true},
        {"puts", I32, {Ptr}},
        {"sceKernelOpen", I32, {Ptr, I32, I32}},
        {"sceKernelClose", I32, {I32}},
        {"sceKernelRead", I32, {I32, Ptr, U64}},
        {"sceKernelWrite", I32, {I32, Ptr, U64}},
        {"sceKernelMmap", Ptr, {Ptr, U64, I32, I32, I32, U64}},
        {"sceKernelMunmap", I32, {Ptr, U64}},
        {"sceVideoOutOpen", I32, {I32, I32, I32, Ptr}},
        {"scePadCreate", I32, {I32}},
        {"scePadRead", I32, {I32, Ptr, I32}},
        {"sceAudioOutOpen", I32, {I32, I32, I32, I32}},
        {"sceGnmDrawIndex", U32, {Ptr, U32, Ptr}},
        {"sceAgcCreate", I32, {Ptr}},
        {"sceSaveDataMount", I32, {Ptr, Ptr}},
        {"sceHttpCreate", I32, {Ptr, I32}},
        {"sceNetSend", I32, {I32, Ptr, U64, I32}},
        {"sceRtcGetCurrentTick", I32, {Ptr}},
        {"sceRandomGetRandomNumber", I32, {Ptr, U64}},
        {"sceDbgLoggingHandler", U32, {I32, Ptr}},
        {"sceDbgSetMinimumLogLevel", U32, {I32}},
        {"sceKernelAllocateDirectMemory", I32, {U64, U64, U64, I32, Ptr}},
        {"sceKernelMapDirectMemory", I32, {Ptr, U64, I32, I32, U64, Ptr}},
        {"exit", Void, {I32}},
        {"abort", Void, {}},
        {"rand", I32, {}},
        {"srand", Void, {U32}},
        {"qsort", Void, {Ptr, U64, U64, Ptr}},
        {"strlen", U64, {Ptr}},
        {"strcmp", I32, {Ptr, Ptr}},
        {"strcpy", Ptr, {Ptr, Ptr}},
        {"fopen", Ptr, {Ptr, Ptr}},
        {"fclose", I32, {Ptr}},
    };
    return kSeeds;
}

const FunctionSignature* FindSignature(std::string_view function_name) {
    static const auto kIndex = [] {
        std::unordered_map<std::string_view, const FunctionSignature*> index;
        for (const auto& sig : SeedSignatures()) {
            index.emplace(sig.name, &sig);
        }
        return index;
    }();
    const auto it = kIndex.find(function_name);
    return it == kIndex.end() ? nullptr : it->second;
}

std::string_view AbiTypeName(AbiType type) {
    switch (type) {
    case U32:
        return "uint32_t";
    case U64:
        return "uint64_t";
    case I32:
        return "int32_t";
    case I64:
        return "int64_t";
    case F32:
        return "float";
    case F64:
        return "double";
    case Ptr:
        return "void*";
    case Void:
    default:
        return "void";
    }
}

std::string FormatPrototype(const FunctionSignature& sig) {
    std::string out;
    out += AbiTypeName(sig.return_type);
    out += ' ';
    out += sig.name;
    out += '(';
    for (size_t i = 0; i < sig.params.size(); i++) {
        if (i > 0) {
            out += ", ";
        }
        out += AbiTypeName(sig.params[i]);
    }
    if (sig.variadic) {
        out += sig.params.empty() ? "..." : ", ...";
    } else if (sig.params.empty()) {
        out += "void";
    }
    out += ')';
    return out;
}

namespace {

constexpr std::string_view kKernelSystem = "Kernel & System";
constexpr std::string_view kGraphicsVideo = "Graphics & Video";
constexpr std::string_view kAudio = "Audio";
constexpr std::string_view kInputVr = "Input & VR";
constexpr std::string_view kMediaCodecs = "Media Codecs";
constexpr std::string_view kNetwork = "Network";
constexpr std::string_view kNpOnline = "PSN & Online";
constexpr std::string_view kStorageFiles = "Storage & Files";
constexpr std::string_view kDialogsUi = "Dialogs & UI";
constexpr std::string_view kUtility = "Utility";
constexpr std::string_view kDebugDev = "Debug / Dev";
constexpr std::string_view kOtherSystem = "Other System";
constexpr std::string_view kGameThirdParty = "Game & Third-party";

const std::unordered_map<std::string_view, std::string_view>& LibraryCategories() {
    static const std::unordered_map<std::string_view, std::string_view> kTable = {
        {"libc", kKernelSystem},
        {"libkernel", kKernelSystem},
        {"libkernel_module_extension_nosubmission", kKernelSystem},
        {"libSceAcm", kAudio},
        {"libSceAddressSanitizer_nosubmission", kDebugDev},
        {"libSceAgc", kGraphicsVideo},
        {"libSceAgcDriver", kGraphicsVideo},
        {"libSceAjm", kAudio},
        {"libSceAmpr", kKernelSystem},
        {"libSceAppContent", kStorageFiles},
        {"libSceAt9Enc", kAudio},
        {"libSceAudio3d", kAudio},
        {"libSceAudiodec", kAudio},
        {"libSceAudiodecCpu", kAudio},
        {"libSceAudiodecCpuHevag", kAudio},
        {"libSceAudioIn", kAudio},
        {"libSceAudioOut", kAudio},
        {"libSceAudioOut2", kAudio},
        {"libSceAudioPropagation", kAudio},
        {"libSceAvPlayer", kMediaCodecs},
        {"libSceCesCs", kKernelSystem},
        {"libSceCommonDialog", kDialogsUi},
        {"libSceContentDelete", kStorageFiles},
        {"libSceContentExport", kStorageFiles},
        {"libSceContentSearch", kStorageFiles},
        {"libSceConvertKeycode", kInputVr},
        {"libSceCoredump", kDebugDev},
        {"libSceCoredump_nosubmission", kDebugDev},
        {"libSceDbg_nosubmission", kDebugDev},
        {"libSceDbgAudioOut2_nosubmission", kDebugDev},
        {"libSceDbgAudioOut_nosubmission", kDebugDev},
        {"libSceDeci5_nosubmission", kDebugDev},
        {"libSceErrorDialog", kDialogsUi},
        {"libSceFiber", kKernelSystem},
        {"libSceFont", kDialogsUi},
        {"libSceFontFt", kDialogsUi},
        {"libSceFontGsm", kDialogsUi},
        {"libSceFrontPanelDisplay_nosubmission", kDebugDev},
        {"libSceGameLiveStreaming", kNpOnline},
        {"libSceGameUpdate", kNpOnline},
        {"libSceGpuTrace_nosubmission", kDebugDev},
        {"libSceHmd2", kInputVr},
        {"libSceHmd2Reprojection_nosubmission", kInputVr},
        {"libSceHttp", kNetwork},
        {"libSceHttp2", kNetwork},
        {"libSceIme", kInputVr},
        {"libSceImeBackend", kInputVr},
        {"libSceImeDialog", kDialogsUi},
        {"libSceJobManager", kKernelSystem},
        {"libSceJpegDec", kMediaCodecs},
        {"libSceJpegEnc", kMediaCodecs},
        {"libSceJson2", kUtility},
        {"libSceKeyboard", kInputVr},
        {"libSceLoginDialog", kDialogsUi},
        {"libSceLoginService", kNpOnline},
        {"libSceM4aacEnc", kAudio},
        {"libSceMat_nosubmission", kDebugDev},
        {"libSceMouse", kInputVr},
        {"libSceMouseExtension_nosubmission", kInputVr},
        {"libSceMsgDialog", kDialogsUi},
        {"libSceNet", kNetwork},
        {"libSceNet_nosubmission", kNetwork},
        {"libSceNetCtl", kNetwork},
        {"libSceNetCtlAp", kNetwork},
        {"libSceNetCtlApDialog", kDialogsUi},
        {"libSceNgs2", kAudio},
        {"libSceNpAuth", kNpOnline},
        {"libSceNpAuthAuthorizedAppDialog", kDialogsUi},
        {"libSceNpCommerce", kNpOnline},
        {"libSceNpCppWebApi", kNpOnline},
        {"libSceNpEntitlementAccess", kNpOnline},
        {"libSceNpGameIntent", kNpOnline},
        {"libSceNpManager", kNpOnline},
        {"libSceNpSessionSignaling", kNpOnline},
        {"libSceNpTrophy2", kNpOnline},
        {"libSceNpUniversalDataSystem", kNpOnline},
        {"libSceNpUtility", kNpOnline},
        {"libSceNpWebApi2", kNpOnline},
        {"libScePad", kInputVr},
        {"libScePerf_nosubmission", kDebugDev},
        {"libScePfs", kStorageFiles},
        {"libScePlayerInvitationDialog", kDialogsUi},
        {"libScePlayerSelectionDialog", kDialogsUi},
        {"libScePlayGo", kStorageFiles},
        {"libScePlayGo_nosubmission", kStorageFiles},
        {"libScePlayGoDialog", kDialogsUi},
        {"libScePngDec", kMediaCodecs},
        {"libScePngEnc", kMediaCodecs},
        {"libScePosix", kKernelSystem},
        {"libSceProprietaryVoiceChatHelper", kNpOnline},
        {"libScePsml", kKernelSystem},
        {"libSceRandom", kUtility},
        {"libSceRazorCpu", kDebugDev},
        {"libSceRazorCpu_debug_nosubmission", kDebugDev},
        {"libSceRemoteplay", kNpOnline},
        {"libSceRtc", kUtility},
        {"libSceRudp", kNetwork},
        {"libSceSaveData", kStorageFiles},
        {"libSceSaveDataDialog", kDialogsUi},
        {"libSceShare", kNpOnline},
        {"libSceSharePlay", kNpOnline},
        {"libSceSigninDialog", kDialogsUi},
        {"libSceSsl", kNetwork},
        {"libSceSulpha", kNetwork},
        {"libSceSulpha_nosubmission", kNetwork},
        {"libSceSysmodule", kKernelSystem},
        {"libSceSystemGesture", kKernelSystem},
        {"libSceSystemService", kKernelSystem},
        {"libSceSystemServiceTournament", kNpOnline},
        {"libSceTextToSpeech2", kAudio},
        {"libSceThreadSanitizer_nosubmission", kDebugDev},
        {"libSceUBSanitizer_nosubmission", kDebugDev},
        {"libSceUlt", kKernelSystem},
        {"libSceUserService", kKernelSystem},
        {"libSceVdecsw", kGraphicsVideo},
        {"libSceVideodec2", kGraphicsVideo},
        {"libSceVideoOut", kGraphicsVideo},
        {"libSceVideoOut_nosubmission", kGraphicsVideo},
        {"libSceVideoRecording_nosubmission", kGraphicsVideo},
        {"libSceVoice", kAudio},
        {"libSceVoiceChat", kNpOnline},
        {"libSceVoiceQoS", kNpOnline},
        {"libSceVrHand", kInputVr},
        {"libSceVrSetupDialog", kDialogsUi},
        {"libSceVrTracker2", kInputVr},
        {"libSceWebBrowserDialog", kDialogsUi},
        {"libSceWorkspace_nosubmission", kDebugDev},
        {"libSceXml", kUtility},
    };
    return kTable;
}

bool IsSonyLibrary(std::string_view library) {
    return library.starts_with("libSce") || library.starts_with("libkernel") || library == "libc" ||
           library == "libm" || library == "libc++" || library == "libc++abi";
}

} // namespace

std::string InferCategory(std::string_view library) {
    const auto& table = LibraryCategories();
    if (const auto it = table.find(library); it != table.end()) {
        return std::string(it->second);
    }
    std::string_view base = library;
    bool stripped = true;
    while (stripped) {
        stripped = false;
        for (std::string_view suffix : {"_nosubmission", "_native", ".native"}) {
            if (base.size() > suffix.size() && base.ends_with(suffix)) {
                base.remove_suffix(suffix.size());
                stripped = true;
            }
        }
    }
    if (base != library) {
        if (const auto it = table.find(base); it != table.end()) {
            return std::string(it->second);
        }
        library = base;
    }

    if (!IsSonyLibrary(library)) {
        return std::string(kGameThirdParty);
    }

    std::string lower(library);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto has = [&](std::string_view needle) { return lower.find(needle) != std::string::npos; };
    if (has("kernel") || has("posix") || has("pthread")) {
        return std::string(kKernelSystem);
    }
    if (has("audio") || has("acm") || has("ngs")) {
        return std::string(kAudio);
    }
    if (has("video") || has("gnm") || has("agc")) {
        return std::string(kGraphicsVideo);
    }
    if (has("pad") || has("camera")) {
        return std::string(kInputVr);
    }
    if (has("net") || has("http") || has("ssl")) {
        return std::string(kNetwork);
    }
    if (has("save") || has("pfs")) {
        return std::string(kStorageFiles);
    }
    if (has("dialog")) {
        return std::string(kDialogsUi);
    }
    if (has("scenp")) {
        return std::string(kNpOnline);
    }
    return std::string(kOtherSystem);
}

} // namespace Core::Analysis
