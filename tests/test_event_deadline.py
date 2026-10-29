import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class EventDeadlineTests(unittest.TestCase):
    def test_pending_timers_sleep_without_early_spin(self):
        source = (ROOT / 'src/app/main.c').read_text()
        start = source.index('static int deadline_wait_ms(')
        end = source.index('\n}', start) + 2
        function = source[start:end]
        model = r'''
#include <assert.h>
#include <math.h>
FUNCTION
int main(void) {
    assert(deadline_wait_ms(1000, INFINITY) == 1000);
    assert(deadline_wait_ms(1000, NAN) == 1000);
    assert(deadline_wait_ms(50, 60.5) == 50);
    assert(deadline_wait_ms(50, 0) == 0);
    assert(deadline_wait_ms(50, -1) == 0);
    assert(deadline_wait_ms(0, 0.2) == 0);
    assert(deadline_wait_ms(50, 1) == 1);
    for (int i = 1; i < 10000; i++) {
        const double deadline = i / 1000.0;
        double now = 0;
        int early_polls = 0, iterations = 0;
        while (now < deadline && iterations++ < 1000) {
            int wait = deadline_wait_ms(1000, deadline - now);
            if (!wait) early_polls++;
            now += wait ? wait : 0.01;
        }
        assert(early_polls == 0);
        assert(now >= deadline && now - deadline < 1.0);
    }
}
'''.replace('FUNCTION', function)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'test.c').write_text(model)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', str(path / 'test.c'), '-lm', '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
            # Demonstrate that the old truncation spins before the timer is due.
            (path / 'test.c').write_text(model.replace('(int)ceil(remaining)', '(int)remaining'))
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', str(path / 'test.c'),
                            '-lm', '-o', str(path / 'old')], check=True)
            result = subprocess.run([str(path / 'old')], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
