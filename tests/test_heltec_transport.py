from __future__ import annotations

import json
import unittest
from copy import deepcopy
from pathlib import Path

from host.youandeye.contracts import ContractError
from host.youandeye.heltec_transport import (
    HELTEC_TEXT_MAX_CHARS,
    HeltecTransportError,
    commands_for_frame,
    encode_heltec_frame,
    write_heltec_frame,
)


ROOT = Path(__file__).resolve().parents[1]


class FakeSerial:
    def __init__(self) -> None:
        self.written = b""
        self.closed = False
        self.flushed = False

    def open(self) -> None:
        pass

    def write(self, data: bytes) -> int:
        self.written += data
        return len(data)

    def flush(self) -> None:
        self.flushed = True

    def close(self) -> None:
        self.closed = True


class HeltecTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.frame = json.loads(
            (ROOT / "protocol" / "examples" / "frame-thinking.json").read_text(encoding="utf-8")
        )

    def test_thinking_scroll_maps_both_channels(self) -> None:
        self.assertEqual(
            ["EMOTE THINKING", "SCROLL PLEASE WAIT..."], commands_for_frame(self.frame)
        )

    def test_muted_utterance_blanks_oled(self) -> None:
        frame = deepcopy(self.frame)
        frame["channel_policy"]["utterance"] = "mute"
        self.assertEqual(["EMOTE THINKING", "MOUTH BLANK"], commands_for_frame(frame))

    def test_none_restores_affect_driven_mouth(self) -> None:
        frame = deepcopy(self.frame)
        frame["utterance"] = {"mode": "none", "sound": "none"}
        self.assertEqual(["EMOTE THINKING", "MOUTH AUTO"], commands_for_frame(frame))

    def test_sequence_uses_device_owned_character_beat(self) -> None:
        frame = deepcopy(self.frame)
        frame["sequence"] = "celebrate"
        frame["utterance"] = {"mode": "none", "sound": "celebrate"}
        self.assertEqual(["BEAT SUCCESS"], commands_for_frame(frame))

    def test_explicit_utterance_preempts_sequence_choreography(self) -> None:
        frame = deepcopy(self.frame)
        frame["sequence"] = "attention"
        self.assertEqual(
            ["EMOTE THINKING", "SCROLL PLEASE WAIT..."], commands_for_frame(frame)
        )

    def test_text_is_single_line_and_bounded(self) -> None:
        frame = deepcopy(self.frame)
        frame["utterance"] = {"mode": "static", "text": "hello\n" + "x" * 100}
        commands = commands_for_frame(frame)
        self.assertEqual(HELTEC_TEXT_MAX_CHARS, len(commands[1].removeprefix("TEXT ")))
        self.assertEqual(2, len(encode_heltec_frame(frame).splitlines()))

    def test_invalid_frame_is_rejected_before_translation(self) -> None:
        frame = deepcopy(self.frame)
        frame["affect"]["intensity"] = 2
        with self.assertRaises(ContractError):
            commands_for_frame(frame)

    def test_write_uses_non_resetting_serial_settings(self) -> None:
        connection = FakeSerial()
        payload = encode_heltec_frame(self.frame)
        written = write_heltec_frame("COM77", payload, serial_factory=lambda: connection)
        self.assertEqual(len(payload), written)
        self.assertEqual(payload, connection.written)
        self.assertTrue(connection.flushed)
        self.assertTrue(connection.closed)
        self.assertFalse(connection.dtr)
        self.assertFalse(connection.rts)
        self.assertFalse(connection.xonxoff)
        self.assertFalse(connection.rtscts)
        self.assertFalse(connection.dsrdtr)

    def test_write_rejects_unterminated_payload(self) -> None:
        with self.assertRaises(HeltecTransportError):
            write_heltec_frame("COM77", b"EMOTE HAPPY", serial_factory=FakeSerial)


if __name__ == "__main__":
    unittest.main()
