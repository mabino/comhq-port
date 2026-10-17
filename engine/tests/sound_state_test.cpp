#include <doctest/doctest.h>

#include "app/sound_state.h"

TEST_CASE("SoundState defaults and initial values") {
    chq::SoundState sound;
    CHECK(sound.volume() == doctest::Approx(1.0f));
    CHECK_FALSE(sound.muted());
    CHECK(sound.effective_gain() == doctest::Approx(1.0f));
    CHECK(sound.volume_percent() == 100);

    chq::SoundState custom(0.5f, true);
    CHECK(custom.volume() == doctest::Approx(0.5f));
    CHECK(custom.muted());
    CHECK(custom.effective_gain() == doctest::Approx(0.0f));
    CHECK(custom.volume_percent() == 50);
}

TEST_CASE("SoundState volume clamping and stepping") {
    chq::SoundState sound(0.95f, false);
    sound.increase_volume(0.1f);
    CHECK(sound.volume() == doctest::Approx(1.0f));
    CHECK(sound.volume_percent() == 100);

    sound.decrease_volume(0.2f);
    CHECK(sound.volume() == doctest::Approx(0.8f));
    CHECK(sound.effective_gain() == doctest::Approx(0.8f));
    CHECK(sound.volume_percent() == 80);

    sound.set_volume(-0.5f);
    CHECK(sound.volume() == doctest::Approx(0.0f));
    CHECK(sound.volume_percent() == 0);

    sound.set_volume(2.5f);
    CHECK(sound.volume() == doctest::Approx(1.0f));
}

TEST_CASE("SoundState mute toggle") {
    chq::SoundState sound(0.7f, false);
    CHECK(sound.effective_gain() == doctest::Approx(0.7f));

    sound.toggle_mute();
    CHECK(sound.muted());
    CHECK(sound.effective_gain() == doctest::Approx(0.0f));
    CHECK(sound.volume() == doctest::Approx(0.7f));

    sound.toggle_mute();
    CHECK_FALSE(sound.muted());
    CHECK(sound.effective_gain() == doctest::Approx(0.7f));
}

TEST_CASE("SoundState frame delta clamping prevents audio desync from UI stalls") {
    CHECK(chq::SoundState::clamp_frame_delta(16.6) == doctest::Approx(16.6));
    CHECK(chq::SoundState::clamp_frame_delta(25.0) == doctest::Approx(25.0));
    CHECK(chq::SoundState::clamp_frame_delta(-5.0) == doctest::Approx(0.0));
    CHECK(chq::SoundState::clamp_frame_delta(500.0) == doctest::Approx(1000.0 / 60.0));
    CHECK(chq::SoundState::clamp_frame_delta(2000.0) == doctest::Approx(1000.0 / 60.0));
}

TEST_CASE("SoundState max queued audio bytes calculation") {
    CHECK(chq::SoundState::max_queued_audio_bytes(44100, 1, 4, 0.10) == 17640);
}
