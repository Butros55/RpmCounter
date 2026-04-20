#include "auto_shift_profile.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <string.h>

#include "core/logging.h"
#include "telemetry_manager.h"

namespace
{
    constexpr const char *PREF_NAMESPACE = "rpm_cfg";
    constexpr const char *PREF_LEARNED_SHIFT = "learnShift";
    constexpr uint32_t LEARNED_SHIFT_MAGIC = 0x4C534831UL; // "LSH1"
    constexpr unsigned long META_FRESH_TIMEOUT_MS = 5000;
    constexpr unsigned long VEHICLE_SWITCH_DEBOUNCE_MS = 350;
    constexpr unsigned long VEHICLE_CHANGE_PULSE_MS = 1500;
    constexpr unsigned long LEARNING_SAMPLE_INTERVAL_MS = 30;
    constexpr unsigned long LEARNING_SAVE_DEBOUNCE_MS = 4000;
    constexpr unsigned long LEARNING_SAVE_MIN_INTERVAL_MS = 15000;
    constexpr int LEARNING_IDLE_MAX_RPM = 2500;
    constexpr int LEARNING_IDLE_MIN_RPM = 350;
    constexpr float LEARNING_IDLE_THROTTLE_MAX = 0.12f;
    constexpr float LEARNING_SHIFT_THROTTLE_MIN = 0.55f;
    constexpr int LEARNING_SHIFT_MIN_RPM = 2500;
    constexpr int LEARNING_UPSHIFT_DROP_RPM = 250;
    constexpr int LEARNING_NEUTRAL_TARGET_OFFSET_RPM = 2200;
    constexpr int LEARNING_DEFAULT_TARGET_RPM = 7200;
    constexpr int LEARNING_DEFAULT_GT3_TARGET_RPM = 7600;

    struct SourceMetaState
    {
        VehicleIdentity identity{};
        unsigned long lastUpdateMs = 0;
    };

    portMUX_TYPE g_autoShiftMux = portMUX_INITIALIZER_UNLOCKED;
    SourceMetaState g_usbMeta{};
    SourceMetaState g_networkMeta{};
    VehicleIdentity g_activeIdentity{};
    VehicleIdentity g_pendingIdentity{};
    unsigned long g_pendingIdentitySinceMs = 0;
    unsigned long g_lastMetaUpdateMs = 0;
    unsigned long g_lastVehicleChangeMs = 0;
    unsigned long g_vehicleChangePulseUntilMs = 0;
    LearnedShiftProfileStore g_learnedStore{};
    bool g_storeDirty = false;
    unsigned long g_lastStoreDirtyMs = 0;
    unsigned long g_lastStoreSaveMs = 0;
    ResolvedShiftProfileRuntime g_runtime{};
    unsigned long g_lastLearnSampleMs = 0;
    int g_previousLearnGear = 0;
    int g_previousLearnRpm = 0;
    float g_previousLearnThrottle = 0.0f;
    bool g_previousLearnPitLimiter = false;
    int g_currentQualifiedPeakRpm = 0;
    int g_highRpmCandidate = 0;
    uint8_t g_highRpmCandidateHits = 0;

    void clearVehicleIdentity(VehicleIdentity &identity)
    {
        identity = VehicleIdentity{};
    }

    String trimText(const char *value)
    {
        String out = value != nullptr ? String(value) : String();
        out.trim();
        return out;
    }

    void copyText(char *dest, size_t capacity, const String &value)
    {
        if (dest == nullptr || capacity == 0)
        {
            return;
        }
        memset(dest, 0, capacity);
        String trimmed = value;
        trimmed.trim();
        trimmed.toCharArray(dest, capacity);
    }

    String normalizedToken(const String &value)
    {
        String out;
        out.reserve(value.length());
        bool lastWasDash = false;
        for (size_t i = 0; i < value.length(); ++i)
        {
            const unsigned char raw = static_cast<unsigned char>(value[i]);
            if (isalnum(raw) != 0)
            {
                out += static_cast<char>(tolower(raw));
                lastWasDash = false;
                continue;
            }
            if (raw == '-' || raw == '_' || raw == ' ' || raw == '/' || raw == '\\' || raw == ':')
            {
                if (!lastWasDash && out.length() > 0)
                {
                    out += '-';
                    lastWasDash = true;
                }
            }
        }
        while (out.endsWith("-"))
        {
            out.remove(out.length() - 1);
        }
        return out;
    }

    uint8_t computedIdentityConfidence(const String &game,
                                       const String &carId,
                                       const String &carModel,
                                       const String &carClass)
    {
        uint8_t confidence = 0;
        if (!game.isEmpty())
        {
            confidence = 20;
        }
        if (!carClass.isEmpty())
        {
            confidence = max<uint8_t>(confidence, 45);
        }
        if (!carModel.isEmpty())
        {
            confidence = max<uint8_t>(confidence, 65);
        }
        if (!carId.isEmpty())
        {
            confidence = max<uint8_t>(confidence, 85);
        }
        if (!carId.isEmpty() && !carModel.isEmpty() && !carClass.isEmpty())
        {
            confidence = 100;
        }
        return confidence;
    }

    String buildProfileKey(const String &game, const String &carId, const String &carModel, const String &carClass)
    {
        const String gameToken = normalizedToken(game);
        const String idToken = normalizedToken(carId);
        const String modelToken = normalizedToken(carModel);
        const String classToken = normalizedToken(carClass);

        String vehicleToken = idToken;
        if (vehicleToken.isEmpty())
        {
            vehicleToken = modelToken;
        }
        if (vehicleToken.isEmpty() && !classToken.isEmpty())
        {
            vehicleToken = String("class-") + classToken;
        }
        if (vehicleToken.isEmpty())
        {
            vehicleToken = "unknown";
        }

        if (gameToken.isEmpty())
        {
            return vehicleToken;
        }
        return gameToken + "::" + vehicleToken;
    }

    String buildClassKey(const String &game, const String &carClass)
    {
        const String classToken = normalizedToken(carClass);
        if (classToken.isEmpty())
        {
            return String();
        }
        const String gameToken = normalizedToken(game);
        if (gameToken.isEmpty())
        {
            return String("class::") + classToken;
        }
        return gameToken + "::class::" + classToken;
    }

    String identityProfileKey(const VehicleIdentity &identity)
    {
        return trimText(identity.profileKey);
    }

    String identityClassKey(const VehicleIdentity &identity)
    {
        return buildClassKey(trimText(identity.gameName), trimText(identity.carClass));
    }

    bool vehicleIdentityEqual(const VehicleIdentity &lhs, const VehicleIdentity &rhs)
    {
        return lhs.valid == rhs.valid &&
               trimText(lhs.profileKey) == trimText(rhs.profileKey) &&
               trimText(lhs.gameName) == trimText(rhs.gameName) &&
               trimText(lhs.carId) == trimText(rhs.carId) &&
               trimText(lhs.carModel) == trimText(rhs.carModel) &&
               trimText(lhs.carClass) == trimText(rhs.carClass);
    }

    VehicleIdentity sanitizeIdentity(const VehicleIdentity &identity, TelemetryVehicleIdentitySource source, unsigned long sampleMs)
    {
        VehicleIdentity sanitized{};
        const String game = trimText(identity.gameName);
        const String carId = trimText(identity.carId);
        const String carModel = trimText(identity.carModel);
        const String carClass = trimText(identity.carClass);
        String profileKey = trimText(identity.profileKey);

        if (profileKey.isEmpty())
        {
            profileKey = buildProfileKey(game, carId, carModel, carClass);
        }

        const bool valid = identity.valid != 0 || !game.isEmpty() || !carId.isEmpty() || !carModel.isEmpty() || !carClass.isEmpty();
        sanitized.valid = valid ? 1 : 0;
        sanitized.confidence = identity.confidence > 0 ? identity.confidence : computedIdentityConfidence(game, carId, carModel, carClass);
        sanitized.lastChangeMs = sampleMs;
        sanitized.source = static_cast<uint8_t>(source);
        copyText(sanitized.gameName, sizeof(sanitized.gameName), game);
        copyText(sanitized.carId, sizeof(sanitized.carId), carId);
        copyText(sanitized.carModel, sizeof(sanitized.carModel), carModel);
        copyText(sanitized.carClass, sizeof(sanitized.carClass), carClass);
        copyText(sanitized.profileKey, sizeof(sanitized.profileKey), profileKey);
        return sanitized;
    }

    bool identitySuggestsGt3(const VehicleIdentity &identity)
    {
        String haystack = trimText(identity.profileKey);
        haystack += ' ';
        haystack += trimText(identity.carClass);
        haystack += ' ';
        haystack += trimText(identity.carModel);
        haystack += ' ';
        haystack += trimText(identity.gameName);
        haystack.toLowerCase();

        return haystack.indexOf("gt3") >= 0 ||
               haystack.indexOf("gt4") >= 0 ||
               haystack.indexOf("gte") >= 0 ||
               haystack.indexOf("cup") >= 0 ||
               haystack.indexOf("porsche") >= 0 ||
               haystack.indexOf("race") >= 0 ||
               haystack.indexOf("endurance") >= 0 ||
               haystack.indexOf("lmp") >= 0 ||
               haystack.indexOf("lmh") >= 0 ||
               haystack.indexOf("hyperc") >= 0 ||
               cfg.mode == 3;
    }

    void touchStoreUpdateToken(LearnedShiftProfileStore &store)
    {
        store.updateToken = (store.updateToken == UINT32_MAX) ? 1U : (store.updateToken + 1U);
    }

    uint32_t computeStoreChecksum(const LearnedShiftProfileStore &store)
    {
        LearnedShiftProfileStore temp = store;
        temp.checksum = 0;
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&temp);
        uint32_t hash = 2166136261UL;
        for (size_t i = 0; i < sizeof(temp); ++i)
        {
            hash ^= bytes[i];
            hash *= 16777619UL;
        }
        return hash;
    }

    void prepareStoreForSave(LearnedShiftProfileStore &store)
    {
        store.magic = LEARNED_SHIFT_MAGIC;
        store.version = LEARNED_SHIFT_PROFILE_VERSION;
        uint8_t count = 0;
        for (uint8_t i = 0; i < MAX_LEARNED_SHIFT_PROFILES; ++i)
        {
            if (store.profiles[i].valid)
            {
                ++count;
                store.profiles[i].version = LEARNED_SHIFT_PROFILE_VERSION;
            }
        }
        store.count = count;
        store.checksum = computeStoreChecksum(store);
    }

    void resetLearnedStore(LearnedShiftProfileStore &store)
    {
        store = LearnedShiftProfileStore{};
        store.magic = LEARNED_SHIFT_MAGIC;
        store.version = LEARNED_SHIFT_PROFILE_VERSION;
        store.updateToken = 1;
        prepareStoreForSave(store);
    }

    bool learnedStoreValid(const LearnedShiftProfileStore &store)
    {
        return store.magic == LEARNED_SHIFT_MAGIC &&
               store.version == LEARNED_SHIFT_PROFILE_VERSION &&
               store.checksum == computeStoreChecksum(store);
    }

    void loadLearnedStore()
    {
        Preferences prefs;
        if (!prefs.begin(PREF_NAMESPACE, true))
        {
            resetLearnedStore(g_learnedStore);
            return;
        }

        LearnedShiftProfileStore loaded{};
        bool ok = false;
        if (prefs.getBytesLength(PREF_LEARNED_SHIFT) == sizeof(LearnedShiftProfileStore))
        {
            ok = prefs.getBytes(PREF_LEARNED_SHIFT, &loaded, sizeof(loaded)) == sizeof(loaded) &&
                 learnedStoreValid(loaded);
        }
        prefs.end();

        if (ok)
        {
            g_learnedStore = loaded;
        }
        else
        {
            resetLearnedStore(g_learnedStore);
        }
        g_storeDirty = false;
        g_lastStoreDirtyMs = 0;
        g_lastStoreSaveMs = 0;
    }

    void persistLearnedStoreIfNeeded(unsigned long nowMs)
    {
        LearnedShiftProfileStore snapshot{};
        bool shouldSave = false;

        portENTER_CRITICAL(&g_autoShiftMux);
        if (cfg.persistLearnedProfiles &&
            g_storeDirty &&
            g_lastStoreDirtyMs > 0 &&
            (nowMs - g_lastStoreDirtyMs) >= LEARNING_SAVE_DEBOUNCE_MS &&
            (g_lastStoreSaveMs == 0 || (nowMs - g_lastStoreSaveMs) >= LEARNING_SAVE_MIN_INTERVAL_MS))
        {
            prepareStoreForSave(g_learnedStore);
            snapshot = g_learnedStore;
            g_storeDirty = false;
            g_lastStoreSaveMs = nowMs;
            shouldSave = true;
        }
        portEXIT_CRITICAL(&g_autoShiftMux);

        if (!shouldSave)
        {
            return;
        }

        Preferences prefs;
        if (!prefs.begin(PREF_NAMESPACE, false))
        {
            portENTER_CRITICAL(&g_autoShiftMux);
            g_storeDirty = true;
            g_lastStoreDirtyMs = nowMs;
            portEXIT_CRITICAL(&g_autoShiftMux);
            return;
        }
        prefs.putBytes(PREF_LEARNED_SHIFT, &snapshot, sizeof(snapshot));
        prefs.end();
    }

    void markStoreDirty(unsigned long nowMs)
    {
        g_storeDirty = true;
        g_lastStoreDirtyMs = nowMs;
        touchStoreUpdateToken(g_learnedStore);
    }

    LearnedShiftProfile *findLearnedProfileByKey(const char *profileKey)
    {
        if (profileKey == nullptr || profileKey[0] == '\0')
        {
            return nullptr;
        }
        const String wanted = trimText(profileKey);
        for (uint8_t i = 0; i < MAX_LEARNED_SHIFT_PROFILES; ++i)
        {
            if (g_learnedStore.profiles[i].valid &&
                trimText(g_learnedStore.profiles[i].identity.profileKey) == wanted)
            {
                return &g_learnedStore.profiles[i];
            }
        }
        return nullptr;
    }

    LearnedShiftProfile *findLearnedProfileByClassKey(const String &classKey)
    {
        if (classKey.isEmpty())
        {
            return nullptr;
        }

        LearnedShiftProfile *best = nullptr;
        for (uint8_t i = 0; i < MAX_LEARNED_SHIFT_PROFILES; ++i)
        {
            LearnedShiftProfile &profile = g_learnedStore.profiles[i];
            if (!profile.valid || trimText(profile.classKey) != classKey)
            {
                continue;
            }
            if (best == nullptr ||
                profile.sampleCount > best->sampleCount ||
                profile.lastUpdatedToken > best->lastUpdatedToken)
            {
                best = &profile;
            }
        }
        return best;
    }

    void initializeLearnedProfile(LearnedShiftProfile &profile, const VehicleIdentity &identity)
    {
        profile = LearnedShiftProfile{};
        profile.valid = 1;
        profile.version = LEARNED_SHIFT_PROFILE_VERSION;
        profile.identity = identity;
        copyText(profile.classKey, sizeof(profile.classKey), identityClassKey(identity));
        profile.confidence = identity.confidence;
    }

    LearnedShiftProfile *ensureLearnedProfile(const VehicleIdentity &identity, unsigned long nowMs)
    {
        if (!identity.valid)
        {
            return nullptr;
        }

        LearnedShiftProfile *existing = findLearnedProfileByKey(identity.profileKey);
        if (existing != nullptr)
        {
            return existing;
        }

        LearnedShiftProfile *target = nullptr;
        for (uint8_t i = 0; i < MAX_LEARNED_SHIFT_PROFILES; ++i)
        {
            if (!g_learnedStore.profiles[i].valid)
            {
                target = &g_learnedStore.profiles[i];
                break;
            }
        }

        if (target == nullptr)
        {
            for (uint8_t i = 0; i < MAX_LEARNED_SHIFT_PROFILES; ++i)
            {
                LearnedShiftProfile &candidate = g_learnedStore.profiles[i];
                if (candidate.locked)
                {
                    continue;
                }
                if (target == nullptr || candidate.lastUpdatedToken < target->lastUpdatedToken)
                {
                    target = &candidate;
                }
            }
        }

        if (target == nullptr)
        {
            return nullptr;
        }

        initializeLearnedProfile(*target, identity);
        markStoreDirty(nowMs);
        return target;
    }

    uint16_t fallbackShiftRpmForSlot(const LearnedShiftProfile *profile, int slot, bool gt3Like)
    {
        if (profile != nullptr && slot >= 0 && slot < LEARNED_SHIFT_PROFILE_GEAR_SLOTS)
        {
            const PerGearLearnStats &stats = profile->gearStats[slot];
            if (stats.learnedShiftRpm > 0)
            {
                return stats.learnedShiftRpm;
            }
            if (stats.observedPeakRpm > 0)
            {
                return static_cast<uint16_t>(max(2000, static_cast<int>(stats.observedPeakRpm) - 140));
            }
        }

        if (profile != nullptr && profile->learnedMaxRpm > 0)
        {
            return static_cast<uint16_t>(max(2000, static_cast<int>(profile->learnedMaxRpm) - 180));
        }

        return static_cast<uint16_t>(gt3Like ? LEARNING_DEFAULT_GT3_TARGET_RPM : LEARNING_DEFAULT_TARGET_RPM);
    }

    void applyLearnedThresholdOverrides(ShiftLightGearProfile &gearProfile,
                                        const PerGearLearnStats *stats,
                                        uint16_t fallbackShiftRpm)
    {
        const uint16_t shiftRpm = (stats != nullptr && stats->learnedShiftRpm > 0) ? stats->learnedShiftRpm : fallbackShiftRpm;
        gearProfile.targetShiftRpm = shiftRpm;

        if (stats != nullptr && stats->valid)
        {
            gearProfile.finalBlinkStartRpm = stats->learnedBlinkRpm > 0
                                                 ? stats->learnedBlinkRpm
                                                 : static_cast<uint16_t>(max(1200, static_cast<int>(shiftRpm) - 65));
            gearProfile.overRevFlashRpm = stats->learnedOverRevRpm > 0
                                              ? stats->learnedOverRevRpm
                                              : static_cast<uint16_t>(shiftRpm + 80);
        }
        else
        {
            gearProfile.finalBlinkStartRpm =
                static_cast<uint16_t>(max<int>(gearProfile.finalBlinkStartRpm, shiftRpm > 70 ? (shiftRpm - 70) : shiftRpm));
            gearProfile.overRevFlashRpm = static_cast<uint16_t>(max<int>(gearProfile.overRevFlashRpm, shiftRpm + 80));
        }
    }

    void buildAutoGeneratedProfileFromLearning(const VehicleIdentity &identity,
                                               const LearnedShiftProfile *profile,
                                               ShiftLightAdvancedProfile &generated)
    {
        generated = ShiftLightAdvancedProfile{};
        generated.enabled = 1;
        generated.useGearOverrides = 1;
        generated.fastResponseForSim = 1;
        generated.allowNeutralProfile = 1;
        const bool gt3Like = identitySuggestsGt3(identity);
        const ShiftLightLayoutMode layout = gt3Like ? ShiftLightLayoutMode::Gt3Mirrored15 : ShiftLightLayoutMode::Linear30;
        generated.layout = static_cast<uint8_t>(layout);

        const uint16_t baseTarget = fallbackShiftRpmForSlot(profile, 1, gt3Like);
        shiftLightGenerateGearProfilePreset(generated.defaultProfile, layout, baseTarget);
        generated.defaultProfile.enabled = 1;

        const uint16_t idleRpm = profile != nullptr && profile->learnedIdleRpm > 0
                                     ? profile->learnedIdleRpm
                                     : static_cast<uint16_t>(max(700, baseTarget - LEARNING_NEUTRAL_TARGET_OFFSET_RPM));
        const uint16_t neutralTarget = static_cast<uint16_t>(max<int>(idleRpm + 1400, baseTarget - LEARNING_NEUTRAL_TARGET_OFFSET_RPM));
        shiftLightGenerateGearProfilePreset(generated.gearProfiles[SHIFTLIGHT_ADVANCED_NEUTRAL_SLOT], layout, neutralTarget);
        generated.gearProfiles[SHIFTLIGHT_ADVANCED_NEUTRAL_SLOT].enabled = 1;

        for (uint8_t slot = 1; slot < SHIFTLIGHT_ADVANCED_GEAR_SLOT_COUNT; ++slot)
        {
            const uint16_t target = fallbackShiftRpmForSlot(profile, slot, gt3Like);
            shiftLightGenerateGearProfilePreset(generated.gearProfiles[slot], layout, target);
            generated.gearProfiles[slot].enabled = 1;
            const PerGearLearnStats *stats =
                (profile != nullptr && slot < LEARNED_SHIFT_PROFILE_GEAR_SLOTS) ? &profile->gearStats[slot] : nullptr;
            applyLearnedThresholdOverrides(generated.gearProfiles[slot], stats, target);
        }

        shiftLightPrepareAdvancedProfileForSave(generated, cfg.greenColor, cfg.yellowColor, effectiveRedColor());
    }

    uint16_t currentGearLearnedShiftRpm(const LearnedShiftProfile *profile, int gear)
    {
        if (profile == nullptr || gear < 0 || gear >= LEARNED_SHIFT_PROFILE_GEAR_SLOTS)
        {
            return 0;
        }
        return profile->gearStats[gear].learnedShiftRpm;
    }

    uint16_t currentGearLearnedBlinkRpm(const LearnedShiftProfile *profile, int gear)
    {
        if (profile == nullptr || gear < 0 || gear >= LEARNED_SHIFT_PROFILE_GEAR_SLOTS)
        {
            return 0;
        }
        return profile->gearStats[gear].learnedBlinkRpm;
    }

    void updateIdleLearning(LearnedShiftProfile &profile, const TelemetryRenderSnapshot &snapshot)
    {
        if (snapshot.rpm < LEARNING_IDLE_MIN_RPM ||
            snapshot.rpm > LEARNING_IDLE_MAX_RPM ||
            snapshot.throttle > LEARNING_IDLE_THROTTLE_MAX ||
            snapshot.pitLimiter)
        {
            return;
        }
        if (snapshot.gear > 1 && snapshot.speedKmh > 8)
        {
            return;
        }

        if (profile.learnedIdleRpm == 0)
        {
            profile.learnedIdleRpm = static_cast<uint16_t>(snapshot.rpm);
        }
        else
        {
            profile.learnedIdleRpm = static_cast<uint16_t>((static_cast<uint32_t>(profile.learnedIdleRpm) * 7U +
                                                            static_cast<uint32_t>(snapshot.rpm)) /
                                                           8U);
        }
    }

    void updateHighRpmLearning(LearnedShiftProfile &profile, const TelemetryRenderSnapshot &snapshot)
    {
        if (snapshot.gear < 1 || snapshot.gear >= LEARNED_SHIFT_PROFILE_GEAR_SLOTS)
        {
            return;
        }
        if (snapshot.pitLimiter || snapshot.throttle < LEARNING_SHIFT_THROTTLE_MIN || snapshot.rpm < LEARNING_SHIFT_MIN_RPM)
        {
            return;
        }

        PerGearLearnStats &stats = profile.gearStats[snapshot.gear];
        stats.valid = 1;
        ++stats.throttleQualifiedSamples;
        if (snapshot.rpm > stats.observedPeakRpm)
        {
            stats.observedPeakRpm = static_cast<uint16_t>(snapshot.rpm);
        }

        if (snapshot.rpm >= g_highRpmCandidate)
        {
            if (snapshot.rpm > g_highRpmCandidate)
            {
                g_highRpmCandidate = snapshot.rpm;
                g_highRpmCandidateHits = 1;
            }
            else if (g_highRpmCandidateHits < 255)
            {
                ++g_highRpmCandidateHits;
            }
        }
        else if ((g_highRpmCandidate - snapshot.rpm) > 180)
        {
            g_highRpmCandidate = snapshot.rpm;
            g_highRpmCandidateHits = 1;
        }

        if (g_highRpmCandidateHits >= 3)
        {
            profile.learnedMaxRpm = max<uint16_t>(profile.learnedMaxRpm, static_cast<uint16_t>(g_highRpmCandidate));
        }

        g_currentQualifiedPeakRpm = max(g_currentQualifiedPeakRpm, snapshot.rpm);
    }

    void updateShiftLearning(LearnedShiftProfile &profile, const TelemetryRenderSnapshot &snapshot, unsigned long nowMs)
    {
        const bool upshiftDetected =
            g_previousLearnGear >= 1 &&
            g_previousLearnGear < 8 &&
            snapshot.gear == (g_previousLearnGear + 1) &&
            g_previousLearnThrottle >= LEARNING_SHIFT_THROTTLE_MIN &&
            !g_previousLearnPitLimiter &&
            g_previousLearnRpm >= LEARNING_SHIFT_MIN_RPM &&
            snapshot.rpm <= (g_previousLearnRpm - LEARNING_UPSHIFT_DROP_RPM);

        if (!upshiftDetected)
        {
            return;
        }

        const int sourceGear = g_previousLearnGear;
        const int eventRpm = max(g_currentQualifiedPeakRpm, g_previousLearnRpm);
        if (eventRpm < LEARNING_SHIFT_MIN_RPM)
        {
            return;
        }

        PerGearLearnStats &stats = profile.gearStats[sourceGear];
        stats.valid = 1;
        stats.observedPeakRpm = max<uint16_t>(stats.observedPeakRpm, static_cast<uint16_t>(eventRpm));
        stats.sampleCount = static_cast<uint16_t>(min<uint32_t>(65535U, stats.sampleCount + 1U));
        stats.upshiftEvents = static_cast<uint16_t>(min<uint32_t>(65535U, stats.upshiftEvents + 1U));

        if (stats.learnedShiftRpm == 0)
        {
            stats.learnedShiftRpm = static_cast<uint16_t>(eventRpm);
        }
        else
        {
            stats.learnedShiftRpm = static_cast<uint16_t>((static_cast<uint32_t>(stats.learnedShiftRpm) * 3U +
                                                           static_cast<uint32_t>(eventRpm)) /
                                                          4U);
        }

        stats.learnedBlinkRpm = static_cast<uint16_t>(max(1200, static_cast<int>(stats.learnedShiftRpm) - 65));
        stats.learnedOverRevRpm = static_cast<uint16_t>(max<int>(stats.learnedShiftRpm + 80, eventRpm + 40));
        profile.sampleCount += 1U;
        profile.lastUpdatedToken = g_learnedStore.updateToken + 1U;
        profile.confidence = min<uint16_t>(1000U, static_cast<uint16_t>(profile.confidence + 10U));
        markStoreDirty(nowMs);
    }

    void updateLearningForCurrentProfile(LearnedShiftProfile &profile,
                                         const TelemetryRenderSnapshot &snapshot,
                                         unsigned long nowMs)
    {
        if (profile.locked)
        {
            return;
        }

        updateIdleLearning(profile, snapshot);
        updateHighRpmLearning(profile, snapshot);
        updateShiftLearning(profile, snapshot, nowMs);
        profile.sampleCount += 1U;
        profile.lastUpdatedToken = g_learnedStore.updateToken + 1U;
        profile.confidence = max<uint16_t>(profile.confidence, static_cast<uint16_t>(profile.identity.confidence));
        markStoreDirty(nowMs);
    }

    VehicleIdentity candidateIdentityForSnapshot(const TelemetryRenderSnapshot &snapshot, unsigned long nowMs)
    {
        if (snapshot.source == ActiveTelemetrySource::UsbSim &&
            g_usbMeta.identity.valid &&
            g_usbMeta.lastUpdateMs > 0 &&
            (nowMs - g_usbMeta.lastUpdateMs) <= META_FRESH_TIMEOUT_MS)
        {
            return g_usbMeta.identity;
        }

        if (snapshot.source == ActiveTelemetrySource::SimHubNetwork &&
            g_networkMeta.identity.valid &&
            g_networkMeta.lastUpdateMs > 0 &&
            (nowMs - g_networkMeta.lastUpdateMs) <= META_FRESH_TIMEOUT_MS)
        {
            return g_networkMeta.identity;
        }

        if ((snapshot.source == ActiveTelemetrySource::Obd || snapshot.source == ActiveTelemetrySource::None) &&
            g_activeIdentity.valid)
        {
            return g_activeIdentity;
        }

        VehicleIdentity none{};
        return none;
    }

    void resetLearningHotPath()
    {
        g_lastLearnSampleMs = 0;
        g_previousLearnGear = 0;
        g_previousLearnRpm = 0;
        g_previousLearnThrottle = 0.0f;
        g_previousLearnPitLimiter = false;
        g_currentQualifiedPeakRpm = 0;
        g_highRpmCandidate = 0;
        g_highRpmCandidateHits = 0;
    }

    void commitActiveIdentity(const VehicleIdentity &identity, unsigned long nowMs)
    {
        g_activeIdentity = identity;
        g_activeIdentity.lastChangeMs = nowMs;
        g_lastVehicleChangeMs = nowMs;
        g_vehicleChangePulseUntilMs = nowMs + VEHICLE_CHANGE_PULSE_MS;
        resetLearningHotPath();
    }

    void updateActiveIdentity(const TelemetryRenderSnapshot &snapshot, unsigned long nowMs)
    {
        const VehicleIdentity candidate = candidateIdentityForSnapshot(snapshot, nowMs);
        if (vehicleIdentityEqual(candidate, g_activeIdentity))
        {
            g_pendingIdentity = candidate;
            g_pendingIdentitySinceMs = nowMs;
            return;
        }

        if (!vehicleIdentityEqual(candidate, g_pendingIdentity))
        {
            g_pendingIdentity = candidate;
            g_pendingIdentitySinceMs = nowMs;
            return;
        }

        if (g_pendingIdentitySinceMs == 0 ||
            (nowMs - g_pendingIdentitySinceMs) < VEHICLE_SWITCH_DEBOUNCE_MS)
        {
            return;
        }

        commitActiveIdentity(candidate, nowMs);
    }

    bool resolveManualAdvancedProfile(const VehicleIdentity &identity,
                                      int gear,
                                      ResolvedShiftProfileRuntime &runtime,
                                      ResolvedShiftProfileSource source)
    {
        if (!cfg.advancedShiftProfile.enabled)
        {
            return false;
        }

        const ShiftLightResolvedGearProfile resolved = shiftLightResolveGearProfile(cfg.advancedShiftProfile, gear);
        if (!resolved.valid || resolved.profile == nullptr)
        {
            return false;
        }

        runtime.enabled = true;
        runtime.valid = true;
        runtime.autoGenerated = false;
        runtime.usingFallback = resolved.usingFallback;
        runtime.usingGearOverride = !resolved.usingFallback && resolved.resolvedSlot >= 0;
        runtime.profileValid = shiftLightAdvancedProfileValid();
        runtime.fastResponseForSim = cfg.advancedShiftProfile.fastResponseForSim != 0;
        runtime.layout = shiftLightClampLayoutMode(cfg.advancedShiftProfile.layout);
        runtime.source = source;
        runtime.activeGear = gear;
        runtime.resolvedSlot = resolved.resolvedSlot;
        runtime.revision = cfg.advancedShiftProfile.checksum;
        runtime.gearProfile = *resolved.profile;
        copyText(runtime.profileKey, sizeof(runtime.profileKey),
                 source == ResolvedShiftProfileSource::ClassProfile ? identityClassKey(identity)
                                                                   : identityProfileKey(identity));
        return true;
    }

    void assignRuntimeFromGeneratedProfile(const ShiftLightAdvancedProfile &generated,
                                           const VehicleIdentity &identity,
                                           int gear,
                                           ResolvedShiftProfileSource source,
                                           ResolvedShiftProfileRuntime &runtime)
    {
        const ShiftLightResolvedGearProfile resolved = shiftLightResolveGearProfile(generated, gear);
        runtime.enabled = true;
        runtime.valid = resolved.valid && resolved.profile != nullptr;
        runtime.autoGenerated = true;
        runtime.usingFallback = resolved.usingFallback;
        runtime.usingGearOverride = !resolved.usingFallback && resolved.resolvedSlot >= 0;
        runtime.profileValid = runtime.valid;
        runtime.fastResponseForSim = generated.fastResponseForSim != 0;
        runtime.layout = shiftLightClampLayoutMode(generated.layout);
        runtime.source = source;
        runtime.activeGear = gear;
        runtime.resolvedSlot = resolved.resolvedSlot;
        runtime.revision = generated.checksum ^ static_cast<uint32_t>(source);
        if (resolved.profile != nullptr)
        {
            runtime.gearProfile = *resolved.profile;
        }
        copyText(runtime.profileKey, sizeof(runtime.profileKey),
                 source == ResolvedShiftProfileSource::LearnedClassProfile ? identityClassKey(identity)
                                                                          : identityProfileKey(identity));
    }

    void resolveRuntimeProfile(const TelemetryRenderSnapshot &snapshot)
    {
        ResolvedShiftProfileRuntime runtime{};
        const int gear = constrain(snapshot.gear, 0, 8);
        const String vehicleKey = identityProfileKey(g_activeIdentity);
        const String classKey = identityClassKey(g_activeIdentity);
        const bool hasManualVehicleBinding =
            cfg.advancedShiftProfile.enabled &&
            !cfg.manualAdvancedProfileKey.isEmpty() &&
            cfg.manualAdvancedProfileKey == vehicleKey;
        const bool hasManualClassBinding =
            cfg.advancedShiftProfile.enabled &&
            !cfg.manualAdvancedClassKey.isEmpty() &&
            !classKey.isEmpty() &&
            cfg.manualAdvancedClassKey == classKey;

        if (hasManualVehicleBinding &&
            resolveManualAdvancedProfile(g_activeIdentity, gear, runtime, ResolvedShiftProfileSource::ExactVehicleProfile))
        {
            g_runtime = runtime;
            return;
        }

        if (hasManualClassBinding &&
            resolveManualAdvancedProfile(g_activeIdentity, gear, runtime, ResolvedShiftProfileSource::ClassProfile))
        {
            g_runtime = runtime;
            return;
        }

        LearnedShiftProfile *exactLearned =
            g_activeIdentity.valid ? findLearnedProfileByKey(g_activeIdentity.profileKey) : nullptr;
        LearnedShiftProfile *classLearned =
            (g_activeIdentity.valid && exactLearned == nullptr) ? findLearnedProfileByClassKey(classKey) : nullptr;

        if (cfg.autoDetectEnabled && exactLearned != nullptr)
        {
            ShiftLightAdvancedProfile generated{};
            buildAutoGeneratedProfileFromLearning(g_activeIdentity, exactLearned, generated);
            assignRuntimeFromGeneratedProfile(generated, g_activeIdentity, gear, ResolvedShiftProfileSource::LearnedVehicleProfile, runtime);
            g_runtime = runtime;
            return;
        }

        if (cfg.autoDetectEnabled &&
            cfg.autoProfilePreference != AutoShiftProfilePreference::ExactVehicle &&
            classLearned != nullptr)
        {
            ShiftLightAdvancedProfile generated{};
            buildAutoGeneratedProfileFromLearning(g_activeIdentity, classLearned, generated);
            assignRuntimeFromGeneratedProfile(generated, g_activeIdentity, gear, ResolvedShiftProfileSource::LearnedClassProfile, runtime);
            g_runtime = runtime;
            return;
        }

        if (cfg.autoDetectEnabled && g_activeIdentity.valid)
        {
            ShiftLightAdvancedProfile generated{};
            buildAutoGeneratedProfileFromLearning(g_activeIdentity, nullptr, generated);
            assignRuntimeFromGeneratedProfile(
                generated,
                g_activeIdentity,
                gear,
                identitySuggestsGt3(g_activeIdentity) ? ResolvedShiftProfileSource::GenericGt3
                                                      : ResolvedShiftProfileSource::GenericLinear,
                runtime);
            g_runtime = runtime;
            return;
        }

        if (cfg.advancedShiftProfile.enabled &&
            resolveManualAdvancedProfile(g_activeIdentity, gear, runtime, ResolvedShiftProfileSource::ManualSimpleFallback))
        {
            g_runtime = runtime;
            return;
        }

        g_runtime = ResolvedShiftProfileRuntime{};
    }

    TelemetryVehicleLearningInfo buildLearningInfo(unsigned long nowMs)
    {
        TelemetryVehicleLearningInfo info{};
        info.identity = g_activeIdentity;
        info.runtime = g_runtime;
        info.resolvedShiftProfileSource = g_runtime.source;
        info.autoDetectEnabled = cfg.autoDetectEnabled;
        info.autoLearningEnabled = cfg.autoLearningEnabled;
        info.persistLearnedProfiles = cfg.persistLearnedProfiles;
        info.learningVehicleKnown = g_activeIdentity.valid != 0;
        info.profileAutoGenerated = g_runtime.autoGenerated;
        info.profileUsingGearOverride = g_runtime.usingGearOverride;
        info.profileUsingFallback = g_runtime.usingFallback;
        info.profileValid = g_runtime.valid && g_runtime.profileValid;
        info.lastMetaUpdateMs = g_lastMetaUpdateMs;
        info.profileLastVehicleChangeMs = g_lastVehicleChangeMs;
        info.vehicleChangeDetected = g_vehicleChangePulseUntilMs > nowMs;

        const LearnedShiftProfile *profile =
            g_activeIdentity.valid ? findLearnedProfileByKey(g_activeIdentity.profileKey) : nullptr;
        info.currentVehicleLocked = profile != nullptr && profile->locked;
        info.learnedProfileCount = g_learnedStore.count;
        info.learningSamples = profile != nullptr ? profile->sampleCount : 0;
        info.learningMaxRpm = profile != nullptr ? profile->learnedMaxRpm : 0;
        info.learningIdleRpm = profile != nullptr ? profile->learnedIdleRpm : 0;
        const int gear = constrain(g_runtime.activeGear, 0, 8);
        info.learningShiftRpmCurrentGear = currentGearLearnedShiftRpm(profile, gear);
        info.learningBlinkRpmCurrentGear = currentGearLearnedBlinkRpm(profile, gear);
        return info;
    }
}

void autoShiftProfileInit()
{
    portENTER_CRITICAL(&g_autoShiftMux);
    clearVehicleIdentity(g_activeIdentity);
    clearVehicleIdentity(g_pendingIdentity);
    g_usbMeta = SourceMetaState{};
    g_networkMeta = SourceMetaState{};
    g_runtime = ResolvedShiftProfileRuntime{};
    g_pendingIdentitySinceMs = 0;
    g_lastMetaUpdateMs = 0;
    g_lastVehicleChangeMs = 0;
    g_vehicleChangePulseUntilMs = 0;
    resetLearningHotPath();
    portEXIT_CRITICAL(&g_autoShiftMux);

    loadLearnedStore();
}

void autoShiftProfileOnSourceMeta(ActiveTelemetrySource source, const VehicleIdentity &identity, unsigned long sampleMs)
{
    if (source != ActiveTelemetrySource::UsbSim && source != ActiveTelemetrySource::SimHubNetwork)
    {
        return;
    }

    const TelemetryVehicleIdentitySource metaSource =
        source == ActiveTelemetrySource::UsbSim ? TelemetryVehicleIdentitySource::UsbBridge
                                                : TelemetryVehicleIdentitySource::SimHubDirect;
    const VehicleIdentity sanitized = sanitizeIdentity(identity, metaSource, sampleMs);

    portENTER_CRITICAL(&g_autoShiftMux);
    SourceMetaState &state = source == ActiveTelemetrySource::UsbSim ? g_usbMeta : g_networkMeta;
    const bool changed = !vehicleIdentityEqual(state.identity, sanitized);
    state.identity = sanitized;
    state.lastUpdateMs = sampleMs;
    g_lastMetaUpdateMs = max(g_lastMetaUpdateMs, sampleMs);

    if (source == ActiveTelemetrySource::UsbSim)
    {
        ++g_usbTelemetryDebug.metaFramesReceived;
        g_usbTelemetryDebug.lastMetaUpdateMs = sampleMs;
        if (changed)
        {
            ++g_usbTelemetryDebug.metaChangeCount;
            g_usbTelemetryDebug.lastMetaChangeMs = sampleMs;
            g_usbTelemetryDebug.lastVehicleProfileKey = trimText(sanitized.profileKey);
        }
    }
    else
    {
        ++g_simHubDebug.metaUpdateCount;
        g_simHubDebug.lastMetaUpdateMs = sampleMs;
        if (changed)
        {
            ++g_simHubDebug.metaChangeCount;
            g_simHubDebug.lastMetaChangeMs = sampleMs;
            g_simHubDebug.lastVehicleProfileKey = trimText(sanitized.profileKey);
        }
    }
    portEXIT_CRITICAL(&g_autoShiftMux);
}

void autoShiftProfileOnResolvedTelemetry(const TelemetryRenderSnapshot &snapshot, unsigned long nowMs)
{
    portENTER_CRITICAL(&g_autoShiftMux);
    updateActiveIdentity(snapshot, nowMs);

    if (cfg.autoLearningEnabled &&
        (snapshot.source == ActiveTelemetrySource::UsbSim || snapshot.source == ActiveTelemetrySource::SimHubNetwork) &&
        g_activeIdentity.valid &&
        snapshot.telemetryFresh &&
        (g_lastLearnSampleMs == 0 || (nowMs - g_lastLearnSampleMs) >= LEARNING_SAMPLE_INTERVAL_MS))
    {
        LearnedShiftProfile *profile = ensureLearnedProfile(g_activeIdentity, nowMs);
        if (profile != nullptr)
        {
            updateLearningForCurrentProfile(*profile, snapshot, nowMs);
        }
        g_lastLearnSampleMs = nowMs;
    }

    resolveRuntimeProfile(snapshot);

    const int currentGear = constrain(snapshot.gear, 0, 8);
    if (currentGear != g_previousLearnGear)
    {
        g_currentQualifiedPeakRpm = 0;
    }
    g_previousLearnGear = currentGear;
    g_previousLearnRpm = snapshot.rpm;
    g_previousLearnThrottle = snapshot.throttle;
    g_previousLearnPitLimiter = snapshot.pitLimiter;
    if (currentGear <= 0 || snapshot.throttle < LEARNING_SHIFT_THROTTLE_MIN)
    {
        g_currentQualifiedPeakRpm = 0;
    }
    portEXIT_CRITICAL(&g_autoShiftMux);

    persistLearnedStoreIfNeeded(nowMs);
}

void autoShiftProfileGetInfo(TelemetryVehicleLearningInfo &out)
{
    const unsigned long nowMs = millis();
    portENTER_CRITICAL(&g_autoShiftMux);
    out = buildLearningInfo(nowMs);
    portEXIT_CRITICAL(&g_autoShiftMux);
}

void autoShiftProfileCopyRuntime(ResolvedShiftProfileRuntime &out)
{
    portENTER_CRITICAL(&g_autoShiftMux);
    out = g_runtime;
    portEXIT_CRITICAL(&g_autoShiftMux);
}

bool autoShiftProfileResetCurrent()
{
    bool removed = false;
    const unsigned long nowMs = millis();
    portENTER_CRITICAL(&g_autoShiftMux);
    if (g_activeIdentity.valid)
    {
        for (uint8_t i = 0; i < MAX_LEARNED_SHIFT_PROFILES; ++i)
        {
            if (g_learnedStore.profiles[i].valid &&
                trimText(g_learnedStore.profiles[i].identity.profileKey) == trimText(g_activeIdentity.profileKey))
            {
                g_learnedStore.profiles[i] = LearnedShiftProfile{};
                markStoreDirty(nowMs);
                removed = true;
                break;
            }
        }
        prepareStoreForSave(g_learnedStore);
    }
    portEXIT_CRITICAL(&g_autoShiftMux);
    persistLearnedStoreIfNeeded(nowMs + LEARNING_SAVE_MIN_INTERVAL_MS);
    return removed;
}

void autoShiftProfileResetAll()
{
    const unsigned long nowMs = millis();
    portENTER_CRITICAL(&g_autoShiftMux);
    resetLearnedStore(g_learnedStore);
    g_storeDirty = true;
    g_lastStoreDirtyMs = nowMs;
    resetLearningHotPath();
    portEXIT_CRITICAL(&g_autoShiftMux);
    persistLearnedStoreIfNeeded(nowMs + LEARNING_SAVE_MIN_INTERVAL_MS);
}

bool autoShiftProfileCopyCurrentToManualAdvanced()
{
    bool copied = false;
    portENTER_CRITICAL(&g_autoShiftMux);
    if (g_activeIdentity.valid)
    {
        const LearnedShiftProfile *profile = findLearnedProfileByKey(g_activeIdentity.profileKey);
        ShiftLightAdvancedProfile generated{};
        buildAutoGeneratedProfileFromLearning(g_activeIdentity, profile, generated);
        cfg.advancedShiftProfile = generated;
        cfg.advancedShiftProfile.enabled = 1;
        cfg.manualAdvancedProfileKey = identityProfileKey(g_activeIdentity);
        cfg.manualAdvancedClassKey = identityClassKey(g_activeIdentity);
        copied = true;
    }
    portEXIT_CRITICAL(&g_autoShiftMux);

    if (copied)
    {
        saveConfig();
    }
    return copied;
}

bool autoShiftProfileSetCurrentLocked(bool locked)
{
    bool updated = false;
    const unsigned long nowMs = millis();
    portENTER_CRITICAL(&g_autoShiftMux);
    if (g_activeIdentity.valid)
    {
        LearnedShiftProfile *profile = findLearnedProfileByKey(g_activeIdentity.profileKey);
        if (profile != nullptr)
        {
            profile->locked = locked ? 1 : 0;
            profile->lastUpdatedToken = g_learnedStore.updateToken + 1U;
            markStoreDirty(nowMs);
            updated = true;
        }
    }
    portEXIT_CRITICAL(&g_autoShiftMux);
    persistLearnedStoreIfNeeded(nowMs + LEARNING_SAVE_MIN_INTERVAL_MS);
    return updated;
}

bool autoShiftProfileCurrentLocked()
{
    bool locked = false;
    portENTER_CRITICAL(&g_autoShiftMux);
    if (g_activeIdentity.valid)
    {
        const LearnedShiftProfile *profile = findLearnedProfileByKey(g_activeIdentity.profileKey);
        locked = profile != nullptr && profile->locked;
    }
    portEXIT_CRITICAL(&g_autoShiftMux);
    return locked;
}
