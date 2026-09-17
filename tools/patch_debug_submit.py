path = 'src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c'
with open(path) as f:
    content = f.read()

old = '''      ret = panvk_kbase_submit_and_wait(dev->drm_fd, batch->vtc_jc.first_job, 0x16);
      assert(!ret);'''
new = '''      ret = panvk_kbase_submit_and_wait(dev->drm_fd, batch->vtc_jc.first_job, 0x16);
      fprintf(stderr, "[PANVK_DEBUG_SUBMIT] vtc_jc ret=%d\\n", ret);
      assert(!ret);'''
n = content.count(old)
print('vtc_jc anchor count:', n)
assert n == 1
content = content.replace(old, new, 1)

old2 = '''      ret = panvk_kbase_submit_and_wait(dev->drm_fd, batch->frag_jc.first_job, 0x01);
      assert(!ret);'''
new2 = '''      ret = panvk_kbase_submit_and_wait(dev->drm_fd, batch->frag_jc.first_job, 0x01);
      fprintf(stderr, "[PANVK_DEBUG_SUBMIT] frag_jc ret=%d\\n", ret);
      assert(!ret);'''
n2 = content.count(old2)
print('frag_jc anchor count:', n2)
assert n2 == 1
content = content.replace(old2, new2, 1)

with open(path, 'w') as f:
    f.write(content)
print('WRITTEN')
