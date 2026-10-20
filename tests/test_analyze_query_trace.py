import importlib.util
import io
from pathlib import Path
import unittest


TOOL = Path(__file__).resolve().parents[1] / 'tools/analyze_query_trace.py'
SPEC = importlib.util.spec_from_file_location('analyze_query_trace', TOOL)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def call(frame, name, fields='', success=False):
    return (f'Thread 0, Frame {frame}, Time {frame * 1000} us:\n'
            f'{name}() returns {"VkResult VK_SUCCESS (0)" if success else "void"}:\n'
            f'{fields}\n\n')


CB = '    commandBuffer: VkCommandBuffer = 0x1234'
SUBMIT = ('    commandBufferCount: uint32_t = 1\n'
          '    pCommandBuffers[0]: const VkCommandBuffer = 0x1234')


class QueryTraceTests(unittest.TestCase):
    def recording(self, frame=5, secondary=False):
        names = ['vkCmdBeginRenderPass', 'vkCmdDrawIndexed',
                 'vkCmdPipelineBarrier', 'vkCmdEndQuery', 'vkCmdEndRenderPass']
        if secondary:
            names.append('vkCmdExecuteCommands')
        return (call(frame, 'vkBeginCommandBuffer', CB, True) +
                ''.join(call(frame, name, CB) for name in names) +
                call(frame, 'vkEndCommandBuffer', CB, True))

    def analyze(self, text):
        return MODULE.analyze(io.StringIO(text))['frames']

    def test_complete_recording_across_adjacent_frames(self):
        row = self.analyze(self.recording() + call(6, 'vkQueueSubmit', SUBMIT, True))[6]
        self.assertEqual(row['submitted_complete'], 1)
        self.assertEqual(row['complete_commands']['vkCmdEndQuery'], 1)
        self.assertEqual(row['in_render_pass_barriers'], 1)
        self.assertEqual(row['in_render_pass_query_ends'], 1)

    def test_gap_invalidates_previously_seen_recording(self):
        row = self.analyze(self.recording() + call(8, 'vkQueueSubmit', SUBMIT, True))[8]
        self.assertEqual(row['submitted_complete'], 0)
        self.assertEqual(row['submitted_incomplete'], 1)

    def test_missing_begin_or_secondary_is_not_complete(self):
        for source in (call(5, 'vkEndCommandBuffer', CB, True),
                       self.recording(secondary=True)):
            row = self.analyze(source + call(5, 'vkQueueSubmit', SUBMIT, True))[5]
            self.assertEqual(row['submitted_complete'], 0)
            self.assertEqual(row['submitted_incomplete'], 1)

    def test_resubmission_counts_each_execution(self):
        submit = call(5, 'vkQueueSubmit', SUBMIT, True)
        row = self.analyze(self.recording() + submit + submit)[5]
        self.assertEqual(row['submitted_complete'], 2)
        self.assertEqual(row['complete_commands']['vkCmdEndQuery'], 2)

    def test_truncated_submit_is_rejected(self):
        text = call(5, 'vkQueueSubmit', '    commandBufferCount: uint32_t = 1', True)
        row = self.analyze(self.recording() + text)[5]
        self.assertEqual(row['submitted_complete'], 0)
        self.assertEqual(row['unsupported_submit_calls'], 1)


if __name__ == '__main__':
    unittest.main()
