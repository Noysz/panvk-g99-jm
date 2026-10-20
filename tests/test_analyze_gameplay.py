import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / 'tools/analyze_gameplay.py'
SPEC = importlib.util.spec_from_file_location('analyze_gameplay', TOOL)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class GameplayEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.manifest = {
            'stopped_after_result': True, 'timed_out': False, 'failure': None,
            'actions': [{'action': 'BENCHMARK', 'present_lines': 2, 'elapsed_seconds': 10}],
            'screenshots': [{'present_lines': 50, 'elapsed_seconds': 15},
                            {'present_lines': 901, 'elapsed_seconds': 100}],
            'sha256': {'/driver/libvulkan_panfrost.so': 'driver'},
            'controls': {'PANVK_UBO_PUSH': '32', 'VK_LAYER_MESA_OVERLAY_CONFIG':
                         'no_display=1,output_file=/run/present.csv'},
            'initial_state_sha256': {'user.reg': 'state'},
        }
        self.write_manifest()
        (self.path / 'present.csv').write_text(
            'frame, frame_timing(us), submit, draw, draw_indexed, present_timing(us)\n' +
            ', 1, 100000, 2, 20, 200, 500\n' * 900)
        (self.path / 'result-ocr.txt').write_text(
            'BENCHMARK STATISTICS: MIN FPS: 5.0 MAX FPS: 20.0 AVERAGE FPS: 10.0\n')
        (self.path / 'process.log').write_text('info: Buffer size: 848x480\n')

    def write_manifest(self):
        (self.path / 'manifest.json').write_text(json.dumps(self.manifest))

    def analyze(self):
        return MODULE.analyze(self.path, 45, 35)

    def test_present_intervals_not_row_rate(self):
        result = self.analyze()
        self.assertAlmostEqual(result['present_window']['fps'], 10)
        self.assertEqual(result['present_window']['p99_ms'], 100)

    def test_incomplete_benchmark_is_rejected(self):
        self.manifest['stopped_after_result'] = False
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'completed benchmark'):
            self.analyze()

    def test_excluded_or_manually_steered_run_is_rejected(self):
        for name in ('excluded.json', 'manual-actions.json'):
            with self.subTest(name=name):
                marker = self.path / name
                marker.write_text('{}')
                with self.assertRaisesRegex(ValueError, 'excluded or manually steered'):
                    self.analyze()
                marker.unlink()

    def test_diagnostic_profiling_is_not_normal_mode(self):
        for controls in ({'VK_INSTANCE_LAYERS': 'VK_LAYER_MESA_overlay:VK_LAYER_LUNARG_api_dump'},
                         {'PANVK_KBASE_SYNC_SUBMIT': '1'},
                         {'PANVK_KBASE_WAIT_SUBMIT': '1'},
                         {'PANVK_DEFER_OQ_EARLY_CONTROL': '1'}):
            with self.subTest(controls=controls):
                self.manifest['controls'].update(controls)
                self.write_manifest()
                with self.assertRaisesRegex(ValueError, 'diagnostic profiling'):
                    self.analyze()
                for key in controls:
                    del self.manifest['controls'][key]

    def test_query_comparison_holds_ubo_constant(self):
        self.manifest['controls']['PANVK_DEFER_OQ'] = '1'
        self.write_manifest()
        result = MODULE.analyze(self.path, 45, 35, 'PANVK_DEFER_OQ')
        self.assertEqual(result['shared_controls']['PANVK_UBO_PUSH'], '32')
        self.assertNotIn('PANVK_DEFER_OQ', result['shared_controls'])
        self.assertEqual(result['defer_occlusion'], 1)
        result = self.analyze()
        self.assertEqual(result['shared_controls']['PANVK_DEFER_OQ'], '1')

    def test_unknown_comparison_variable_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported comparison variable'):
            MODULE.analyze(self.path, 45, 35, 'PANVK_KBASE_SYNC_SUBMIT')

    def test_swapchain_log_reset_is_rejected(self):
        self.manifest['screenshots'][1]['present_lines'] = 10
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'log reset'):
            self.analyze()

    def test_grouped_frames_are_not_individual_samples(self):
        path = self.path / 'present.csv'
        lines = path.read_text().splitlines()
        lines[600] = ', 2, 100000, 2, 20, 200, 500'
        path.write_text('\n'.join(lines) + '\n')
        with self.assertRaisesRegex(ValueError, 'grouped frames'):
            self.analyze()

    def test_truncated_window_is_rejected(self):
        path = self.path / 'present.csv'
        path.write_text('\n'.join(path.read_text().splitlines()[:700]) + '\n')
        with self.assertRaisesRegex(ValueError, 'incomplete measurement'):
            self.analyze()

    def test_wrong_resolution_is_rejected(self):
        (self.path / 'process.log').write_text('info: Buffer size: 816x1\n')
        with self.assertRaisesRegex(ValueError, '848x480'):
            self.analyze()

    def test_invalid_csv_is_rejected(self):
        (self.path / 'present.csv').write_text('frame,frame_timing(us)\n,1\n')
        with self.assertRaises(ValueError):
            self.analyze()


if __name__ == '__main__':
    unittest.main()
