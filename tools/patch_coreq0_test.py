path = 'src/panfrost/vulkan/jm/panvk_vX_gpu_queue_kbase.c'
with open(path) as f:
    content = f.read()

old = '''   batch->issued = true;'''
new = '''   if (batch->vtc_jc.first_job) {
      int test_ret = panvk_kbase_submit_and_wait(
         dev->kmod.dev->fd, batch->vtc_jc.first_job, 0x00);
      mesa_logd("[PANVK_TEST_COREQ0] resubmit vtc_jc core_req=0x00 ret=%d",
                test_ret);
      fprintf(stderr, "[PANVK_TEST_COREQ0] resubmit vtc_jc core_req=0x00 ret=%d\\n",
              test_ret);
   }

   batch->issued = true;'''

n = content.count(old)
print('anchor count:', n)
assert n == 1, f"Anchor ketemu {n}x, harus persis 1"
content = content.replace(old, new, 1)

with open(path, 'w') as f:
    f.write(content)
print('WRITTEN')
