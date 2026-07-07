import unittest

from scripts.gesture_dashboard import classify_line, parse_telemetry, RepeatedLog, DashboardState


class DashboardParserTests(unittest.TestCase):
    def test_classifies_debug_channels(self):
        self.assertEqual("state", classify_line("[D][STATE] sys=ACTIVE"))
        self.assertEqual("input", classify_line("[D][INPUT] mock button=1"))
        self.assertEqual("gesture", classify_line("[D][GESTURE] engine ready"))
        self.assertEqual("sensor", classify_line("[D][SENS] ACC(0.1,0.2,0.3)"))
        self.assertEqual("other", classify_line("plain boot line"))

    def test_parses_all_gesture_telemetry_fields(self):
        line = ("[D][TELEM] ax=+0.20 ay=-0.10 az=+0.98 mag=1.01 "
                "base=+0.18 motion=+0.02 phase=RETURNING dir=LEFT "
                "shake=0.55 ret=0.39 settle=0.17 win=650 cd=450 inv=0")
        data = parse_telemetry(line)
        self.assertEqual("RETURNING", data["phase"])
        self.assertEqual("LEFT", data["dir"])
        self.assertAlmostEqual(0.20, data["ax"])
        self.assertAlmostEqual(0.18, data["base"])
        self.assertEqual(650, data["win"])

    def test_repeated_log_collapses_consecutive_duplicates(self):
        log = RepeatedLog(limit=3)
        log.add("[D][STATE] sys=ACTIVE")
        log.add("[D][STATE] sys=ACTIVE")
        log.add("[D][STATE] sys=AMBIENT")
        self.assertEqual(2, len(log.items))
        self.assertEqual(2, log.items[0]["count"])
        self.assertEqual("[D][STATE] sys=AMBIENT", log.items[1]["text"])

    def test_phase_history_records_outbound_timeout(self):
        state = DashboardState()
        state.add_line("[D][TELEM] phase=OUTBOUND motion=0.60")
        state.add_line("[D][TELEM] phase=IDLE motion=0.02")
        gesture_lines = [item["text"] for item in state.snapshot()["logs"]["gesture"]]
        self.assertIn("OUTBOUND_TIMEOUT", gesture_lines[-1])


if __name__ == "__main__":
    unittest.main()
