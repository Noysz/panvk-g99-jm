#!/usr/bin/env python3
"""Count query commands in captured API calls and completely recorded submissions."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re


HEADER = re.compile(r'Thread (\d+), Frame (\d+), Time (\d+) us:')
CALL = re.compile(r'(vk\w+)\(')
COMMAND_BUFFER = re.compile(r'^    commandBuffer:\s+VkCommandBuffer = (0x[0-9a-f]+)', re.M)
SUBMITTED = re.compile(r'pCommandBuffers\[\d+\]:\s+const VkCommandBuffer = (0x[0-9a-f]+)')


def blocks(stream):
    header = None
    body = []
    for line in stream:
        match = HEADER.match(line)
        if match:
            if header is not None:
                yield header, ''.join(body)
            header = tuple(map(int, match.groups()))
            body = []
        elif header is not None:
            body.append(line)
    if header is not None:
        yield header, ''.join(body)


def analyze(stream):
    frames = {}
    buffers = {}
    previous = None
    for (thread, frame, timestamp), body in blocks(stream):
        match = CALL.match(body)
        if not match:
            continue
        name = match[1]
        # A trace gap may hide resets or a completely different recording.
        if previous is not None and (frame < previous or frame > previous + 1):
            buffers.clear()
        previous = frame
        row = frames.setdefault(frame, {
            'first_us': timestamp, 'last_us': timestamp, 'calls': Counter(),
            'submitted_complete': 0, 'submitted_incomplete': 0,
            'complete_commands': Counter(), 'in_render_pass_barriers': 0,
            'in_render_pass_query_ends': 0,
            'unsupported_submit_calls': 0,
        })
        row['first_us'] = min(row['first_us'], timestamp)
        row['last_us'] = max(row['last_us'], timestamp)
        row['calls'][name] += 1
        cb = COMMAND_BUFFER.search(body)
        handle = cb[1] if cb else None
        if name == 'vkBeginCommandBuffer' and handle:
            buffers[handle] = {'begun': 'VK_SUCCESS' in body, 'ended': False,
                               'commands': Counter(), 'render': False,
                               'barriers': 0, 'query_ends': 0, 'secondary_unknown': False}
        elif name == 'vkEndCommandBuffer' and handle in buffers:
            buffers[handle]['ended'] = 'VK_SUCCESS' in body
        elif name.startswith('vkCmd') and handle in buffers:
            record = buffers[handle]
            record['commands'][name] += 1
            if name.startswith(('vkCmdBeginRenderPass', 'vkCmdBeginRendering')):
                record['render'] = True
            elif name.startswith(('vkCmdEndRenderPass', 'vkCmdEndRendering')):
                record['render'] = False
            elif name.startswith('vkCmdPipelineBarrier') and record['render']:
                record['barriers'] += 1
            elif name.startswith('vkCmdEndQuery') and record['render']:
                record['query_ends'] += 1
            elif name == 'vkCmdExecuteCommands':
                # Reject these from complete coverage until secondary lifetime is tracked.
                record['secondary_unknown'] = True
        if name == 'vkQueueSubmit':
            handles = SUBMITTED.findall(body)
            declared = sum(map(int, re.findall(r'commandBufferCount:\s+uint32_t = (\d+)', body)))
            if len(handles) != declared or 'VK_SUCCESS' not in body:
                row['unsupported_submit_calls'] += 1
                continue
            for submitted in handles:
                record = buffers.get(submitted)
                if not record or not record['begun'] or not record['ended'] or record['secondary_unknown']:
                    row['submitted_incomplete'] += 1
                else:
                    row['submitted_complete'] += 1
                    row['complete_commands'].update(record['commands'])
                    row['in_render_pass_barriers'] += record['barriers']
                    row['in_render_pass_query_ends'] += record['query_ends']
        elif name.startswith('vkQueueSubmit'):
            row['unsupported_submit_calls'] += 1
    return {'frames': frames, 'scope':
        'Counts are restricted to captured intervals. Complete submissions require '
        'observed Begin/EndCommandBuffer in a contiguous capture, with no secondary '
        'execution. Counts reflect submission occurrences, not distinct buffers. '
        'API frame labels are not automatically scene or presentation matches.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.trace.open() as stream:
        result = analyze(stream)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Captured {len(result["frames"])} frame labels; wrote {args.output}')


if __name__ == '__main__':
    main()
