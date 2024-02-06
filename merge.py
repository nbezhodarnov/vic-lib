import shutil
import sys
import os
import io
import pycriu
import logging
import copy
import resource
import binascii
import psutil

from merge_prepare import merge_prepare_main, merge_prepare_worker
from merge_finish import merge_finish

def _list_to_str(l):
    return '[' + ', '.join(map(str, l)) + ']'

def _get_process_child_pids(pid):
    child_pids = []
    process = psutil.Process(pid)
    for proc in process.children():
        child_pids.append(proc.pid)
    return child_pids

def get_byte_depth():
    return sys.getsizeof(sys.maxsize) - sys.getsizeof(0)

def _parse_backtrace_file(backtrace_file):
    backtrace = []

    while not os.path.exists(backtrace_file):
        pass

    with open(backtrace_file, "rb") as backtrace_file:
        logging.debug(f"Opened backtrace file: {backtrace_file.name}")

        backtrace_file.seek(0, os.SEEK_END)

        end_position = backtrace_file.tell()
        current_position = get_byte_depth() * 2 + 2

        backtrace_file.seek(current_position)

        while current_position + get_byte_depth() <= end_position:
            backtrace_block = {}
            backtrace_block["ip"] = int.from_bytes(backtrace_file.read(get_byte_depth()), signed=False, byteorder='little')
            backtrace_block["sp"] = int.from_bytes(backtrace_file.read(get_byte_depth()), signed=False, byteorder='little')
            backtrace.append(backtrace_block)
            current_position = backtrace_file.tell()

            logging.debug(f"Read backtrace block: {backtrace_block}")

        logging.debug(f"Successfully read backtrace file: {backtrace_file.name}")

    return backtrace

def _max_stack_size(workers_backtrace):
    max_stack_size = 0
    for worker_backtrace in workers_backtrace:
        stack_end = worker_backtrace["backtrace"][-4]["sp"] # -4 to skip main() specific stack frames
        stack_start = worker_backtrace["backtrace"][0]["sp"] + get_byte_depth() * 10 # backtrace doesn't contain current stack frame
        stack_size = stack_end - stack_start
        if stack_size > max_stack_size:
            max_stack_size = stack_size
    return max_stack_size

# def print_stack(stack):
#     byte_depth = get_byte_depth()
#     stack_hex = [binascii.hexlify(stack[i:i + byte_depth]).decode('utf-8') for i in range(0, len(stack), byte_depth)]
#     for line in stack_hex:
#         print(line)

def load_image(image_filepath):
    image_file = io.open(image_filepath, "rb")
    image = pycriu.images.load(image_file)
    image_file.close()
    return image

def save_image(image, image_filepath):
    image_file = io.open(image_filepath, "wb")
    pycriu.images.dump(image, image_file)
    image_file.close()

def get_stack_end_offset_in_pages_file(thread_stack_end, process_pid, input_path):
    pagemap = load_image(input_path + f"/pagemap-{process_pid}.img")
    assert(pagemap['magic'] == 'PAGEMAP')

    pages_id = pagemap['entries'][0]['pages_id']

    pages_size = os.stat(input_path + f"/pages-{pages_id}.img").st_size
    pages_number = pages_size // resource.getpagesize()

    pages_number_after_stack = 0
    page_vaddr_before_stack = 0
    for vma_info in reversed(pagemap['entries']):
        pages_number_after_stack += vma_info['nr_pages']

        vaddr = vma_info['vaddr']
        if vaddr < thread_stack_end:
            page_vaddr_before_stack = vaddr
            break
    logging.debug(f"Pages number after stack: {pages_number_after_stack}")
    logging.debug(f"Page vaddr before stack: {page_vaddr_before_stack}")

    pages_number_before_stack = pages_number - pages_number_after_stack
    logging.debug(f"Vaddr start in pages: {pages_number_before_stack * resource.getpagesize()}")

    stack_end_offset = thread_stack_end - page_vaddr_before_stack

    return pages_number_before_stack * resource.getpagesize() + stack_end_offset

def extract_thread_stack(thread_pid, process_pid, input_path, backtrace):
    thread_core = load_image(input_path + f"/core-{thread_pid}.img")
    assert(thread_core['magic'] == 'CORE')

    thread_info = thread_core['entries'][0]['thread_info']
    thread_sp_reg = thread_info['gpregs']['sp']
    logging.debug(f"Thread {thread_pid} of process {process_pid} SP: {thread_sp_reg}")

    pagemap = load_image(input_path + f"/pagemap-{process_pid}.img")
    assert(pagemap['magic'] == 'PAGEMAP')

    pages_id = pagemap['entries'][0]['pages_id']

    pages_size = os.stat(input_path + f"/pages-{pages_id}.img").st_size
    pages_number = pages_size // resource.getpagesize()

    pages_number_after_stack = 0
    page_vaddr_before_stack = 0
    for vma_info in reversed(pagemap['entries']):
        pages_number_after_stack += vma_info['nr_pages']

        vaddr = vma_info['vaddr']
        if vaddr < thread_sp_reg:
            page_vaddr_before_stack = vaddr
            break
    logging.debug(f"Pages number after stack: {pages_number_after_stack}")
    logging.debug(f"Page vaddr before stack: {page_vaddr_before_stack}")

    pages_number_before_stack = pages_number - pages_number_after_stack
    logging.debug(f"Vaddr start in pages: {pages_number_before_stack * resource.getpagesize()}")

    stack_offset = thread_sp_reg - page_vaddr_before_stack

    pages_file = io.open(input_path + f"/pages-{pages_id}.img", "rb")
    pages_file.seek(pages_number_before_stack * resource.getpagesize() + stack_offset)
    logging.debug(f"Stack start address in pages: {pages_number_before_stack * resource.getpagesize() + stack_offset}")

    byte_depth = get_byte_depth()
    stack = bytes()

    stack_end = backtrace[-1]["sp"]

    stack = pages_file.read(stack_end - thread_sp_reg)
    logging.debug(f"Stack size: {len(stack)}")

    pages_file.close()

    return stack, thread_sp_reg, stack_end, thread_info['gpregs']['bp']

def perform_offset_for_addresses_in_stack(stack, old_sp, old_stack_end, new_stack_end):
    stack_end_offset = new_stack_end - old_stack_end

    edited_stack = bytearray(stack)
    for i in range(0, len(edited_stack), get_byte_depth()):
        address = int.from_bytes(edited_stack[i:i + get_byte_depth()], signed=False, byteorder='little')
        if address >= old_sp and address < old_stack_end:
            address += stack_end_offset
            edited_stack[i:i + get_byte_depth()] = address.to_bytes(get_byte_depth(), byteorder='little')
    
    return bytes(edited_stack)

def extract_fictive_thread_stack_end(thread_pid, process_pid):
    stack_end = 0
    with open(f"/tmp/{process_pid}-{thread_pid}-backtrace.tpl", "rb") as backtrace_file:
        backtrace_file.seek(0, os.SEEK_END)
        end_position = backtrace_file.tell()
        backtrace_file.seek(end_position - get_byte_depth() * 3)
        stack_end = int.from_bytes(backtrace_file.read(get_byte_depth()), signed=False, byteorder='little')
    return stack_end

def perform_register_offset_for_addresses_in_stack(reg, old_sp, old_stack_end, new_stack_end):
    if reg >= old_sp and reg < old_stack_end:
        return reg + new_stack_end - old_stack_end
    return reg

def merge_worker_stack(process_pid, main_pid, thread_pid, input_path, output_path, backtrace):
    stack, sp, worker_stack_end, _ = extract_thread_stack(process_pid, process_pid, input_path, backtrace)

    fictive_thread_stack_end = extract_fictive_thread_stack_end(thread_pid, main_pid)
    logging.debug(f"Fictive thread {thread_pid} from main process {main_pid} stack end: {fictive_thread_stack_end}")

    edited_stack = perform_offset_for_addresses_in_stack(stack, sp, worker_stack_end, fictive_thread_stack_end)

    fictive_thread_stack_end_offset = get_stack_end_offset_in_pages_file(fictive_thread_stack_end, main_pid, input_path)
    logging.debug(f"Fictive thread {thread_pid} from main process {main_pid} stack end offset: {fictive_thread_stack_end_offset}")

    if not os.path.exists(output_path + f"/pages-2.img"):
        shutil.copy(input_path + f"/pages-2.img", output_path + f"/pages-2.img")

    worker_stack_start_in_pages = fictive_thread_stack_end_offset - len(edited_stack)

    pages_file = io.open(output_path + f"/pages-2.img", "rb+")
    pages_file.seek(worker_stack_start_in_pages)
    pages_file.write(edited_stack)
    pages_file.close()

    process_core = load_image(input_path + f"/core-{process_pid}.img")
    assert(process_core['magic'] == 'CORE')

    for reg_name in ["sp", "ax", "bx", "cx", "dx", "si", "di", "bp", "r15", "r14", "r13", "r12"]:
        reg = process_core['entries'][0]['thread_info']['gpregs'][reg_name]
        updated_reg = perform_register_offset_for_addresses_in_stack(reg, sp, worker_stack_end, fictive_thread_stack_end)
        process_core['entries'][0]['thread_info']['gpregs'][reg_name] = updated_reg
    
    process_core['entries'][0].pop("tc")

    thread_core = load_image(input_path + f"/core-{thread_pid}.img")
    assert(thread_core['magic'] == 'CORE')

    process_core['entries'][0]['thread_info']['clear_tid_addr'] = thread_core['entries'][0]['thread_info']['clear_tid_addr']

    process_core['entries'][0]['thread_core'] = thread_core['entries'][0]['thread_core'] # ?

    save_image(process_core, output_path + f"/core-{thread_pid}.img")

def merge_all_workers_stacks(main_pid, processes_threads_relationship, workers_backtrace, input_path, output_path):
    for process_thread in processes_threads_relationship:
        process_pid = process_thread["pid"]
        thread_pid = process_thread["thread_id"]

        logging.debug(f"Moving worker {process_pid} stack to thread {thread_pid}")

        worker_backtrace = None
        for worker_backtrace_item in workers_backtrace:
            if worker_backtrace_item["pid"] == process_pid:
                worker_backtrace = worker_backtrace_item["backtrace"].copy()
                break
        assert(worker_backtrace != None)

        worker_backtrace = worker_backtrace[:-4]

        merge_worker_stack(process_pid, main_pid, thread_pid, input_path, output_path, worker_backtrace)

def merge_in_pstree(main_pid, processes_threads_relationship, input_path, output_path):
    pstree = load_image(input_path + "/pstree.img")
    assert(pstree['magic'] == 'PSTREE')
    assert(len(pstree['entries']) > 1)

    main_process = pstree['entries'][0].copy()
    pstree['entries'].clear()
    pstree['entries'].append(main_process)

    save_image(pstree, output_path + "/pstree.img")

    threads = main_process['threads']
    logging.debug(f"Threads that will stay in dump: {_list_to_str(threads)}")

    return threads

def merge_in_core(main_pid, thread_ids, input_path, output_path):
    for pid in [main_pid] + thread_ids:
        if not os.path.exists(output_path + f"/core-{pid}.img"):
            shutil.copy(input_path + f"/core-{pid}.img", output_path + f"/core-{pid}.img")

def merge_in_fdinfo(input_path, output_path):
    fdinfo = load_image(input_path + "/fdinfo-2.img")
    assert(fdinfo['magic'] == 'FDINFO')

    fdinfo['entries'] = [fd for fd in fdinfo['entries'] if fd['type'] != 'PIPE']

    save_image(fdinfo, output_path + "/fdinfo-2.img")

def merge_in_files(pid, input_path, output_path):
    files = load_image(input_path + "/files.img")
    assert(files['magic'] == 'FILES')

    fs = load_image(input_path + f"/fs-{pid}.img")
    assert(fs['magic'] == 'FS')

    root_id = fs['entries'][0]['root_id']

    files['entries'] = [file for file in files['entries'] if file['id'] <= root_id and file['type'] != 'PIPE']

    save_image(files, output_path + "/files.img")

def copy_main_process_files(main_pid, input_path, output_path):
    for file in ["fs", "ids", "mm", "pagemap"]:
        shutil.copy(input_path + f"/{file}-{main_pid}.img", output_path + f"/{file}-{main_pid}.img")

    for file in ["pages-1.img", "pages-2.img"]:
        if not os.path.exists(os.path.join(output_path, file)):
            shutil.copy(input_path + f"/{file}", output_path + f"/{file}")

def copy_missing_files(input_path, output_path):
    files = [file for file in os.listdir(input_path) if os.path.isfile(os.path.join(input_path, file))]

    shmem_files = [file for file in files if file.startswith("pagemap-shmem")]
    for file in shmem_files:
        shutil.copy(os.path.join(input_path, file), output_path)

    filter_file_prefixes = ["core", "fs", "fdinfo", "ids", "mm", "pagemap", "pages", "pipes"]
    files = [file for file in files if not any(file.startswith(prefix) for prefix in filter_file_prefixes)]

    for file in files:
        if not os.path.exists(os.path.join(output_path, file)):
            shutil.copy(os.path.join(input_path, file), output_path)

def merge(main_pid, processes_threads_relationship, workers_backtrace, input_path, output_path):
    merge_all_workers_stacks(main_pid, processes_threads_relationship, workers_backtrace, input_path, output_path)

    thread_ids = merge_in_pstree(main_pid, processes_threads_relationship, input_path, output_path)

    merge_in_core(main_pid, thread_ids, input_path, output_path)

    merge_in_fdinfo(input_path, output_path)

    merge_in_files(main_pid, input_path, output_path)

    copy_main_process_files(main_pid, input_path, output_path)

    copy_missing_files(input_path, output_path)

def main():
    if len(sys.argv) < 4:
        print("Usage: <input checkpoint path> <output checkpoint path> <program root pid>")
        return

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    pid = int(sys.argv[3])

    logging.basicConfig(level=logging.DEBUG)
    logging.debug(f"Input path: {input_path}")
    logging.debug(f"Output path: {output_path}")

    if not os.path.exists(input_path):
        os.makedirs(input_path)
    else:
        shutil.rmtree(input_path)
        os.makedirs(input_path)
    
    if not os.path.exists(output_path):
        os.makedirs(output_path)
    else:
        shutil.rmtree(output_path)
        os.makedirs(output_path)

    children_pids = []
    for child_pid in _get_process_child_pids(pid):
        if not merge_prepare_worker(child_pid):
            continue
        children_pids.append(child_pid)

    workers_backtrace = []
    for child_pid in children_pids:
        backtrace = _parse_backtrace_file(f"/tmp/{child_pid}-{child_pid}-backtrace.tpl")
        worker_backtrace = {}
        worker_backtrace["pid"] = child_pid
        worker_backtrace["backtrace"] = backtrace
        workers_backtrace.append(worker_backtrace)
    
    max_stack_size = _max_stack_size(workers_backtrace)

    processes_threads_relationship = merge_prepare_main(pid, max_stack_size)
    logging.debug(f"Prosesses threads relationship: {_list_to_str(processes_threads_relationship)}")

    logging.debug("Starting dump")

    result = os.system("criu dump -t " + str(pid) + " -D " + input_path + " -j")
    if result != 0:
        logging.error("Dump failed")
        return
    
    logging.debug("Dump finished")

    logging.debug("Starting merge")

    merge(pid, processes_threads_relationship, workers_backtrace, input_path, output_path)

    logging.debug("Merge finished")

    logging.debug("Starting restore")

    result = os.system("criu restore -D " + output_path + " --restore-detached -j")
    if result != 0:
        logging.error("Restore failed")
        return
    
    logging.debug("Restore finished")

    merge_finish(pid)

if __name__ == "__main__":
    main()
