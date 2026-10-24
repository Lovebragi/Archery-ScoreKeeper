#!/usr/bin/env python3
"""Static contracts for the deep-sleep key-wake guard wiring.

The application's button callback lives in main/archery_app.c (main.c only
boots the BSP and the app), so the guard contract is checked there.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"function is unterminated: {name}")


class KeyWakeGuardWiringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = read("main/archery_app.c")

    def test_on_key_consumes_the_guard_before_the_readiness_check(self) -> None:
        # A release can arrive after the post-init voltage sample but before input
        # dispatch is ready. on_key must run the guard first so that release still
        # disarms it, instead of being discarded by the readiness early-return.
        body = function_body(self.app, "on_key")
        self.assertIn("key_guard_consume(", body)
        self.assertLess(body.index("key_guard_consume("), body.index("!s_ready"))

    def test_guard_is_armed_for_a_gpio_wake_and_synced_after_button_init(self) -> None:
        start = function_body(self.app, "archery_app_start")
        self.assertIn("ESP_SLEEP_WAKEUP_GPIO", start)
        self.assertIn("key_guard_arm(", start)
        self.assertIn("key_guard_sync_released(", start)

    def test_boot_does_not_enter_deep_sleep_mid_session(self) -> None:
        # A training session lives in RAM; this firmware deliberately avoids
        # deep sleep so an unfinished session is never silently discarded.
        self.assertNotIn("esp_deep_sleep_start(", self.app)


if __name__ == "__main__":
    unittest.main()
