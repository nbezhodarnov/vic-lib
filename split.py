import shutil
import sys
import os
import io
import pycriu
import logging
import copy

from split_prepare import split_prepare
from split_finish import split_finish

def _list_to_str(l):
    return '[' + ', '.join(map(str, l)) + ']'

def load_image(image_filepath):
    image_file = io.open(image_filepath, "rb")
    image = pycriu.images.load(image_file)
    image_file.close()
    return image

def save_image(image, image_filepath):
    image_file = io.open(image_filepath, "wb")
    pycriu.images.dump(image, image_file)
    image_file.close()

def get_pids(pstree_filepath):
    pstree = load_image(pstree_filepath)
    
    assert(pstree['magic'] == 'PSTREE')
    assert(len(pstree['entries']) == 1)

    process = pstree['entries'][0]
    return process['threads']

def extract_main_thread_pid(pids):
    main_thread = min(pids)
    return main_thread

def split_in_pstree(input_path, output_path, thread_ids):
    pstree = load_image(input_path + "/pstree.img")
    assert(pstree['magic'] == 'PSTREE')
    assert(len(pstree['entries']) == 1)

    process = pstree['entries'][0]
    threads = process['threads']
    logging.debug(f"Threads: {_list_to_str(threads)}")

    pstree_splited = pstree.copy()
    pstree_splited['entries'] = []

    main_thread = min(threads)
    logging.debug(f"Main thread pid: {main_thread}")

    threads.remove(main_thread)

    worker_threads, additional_threads = thread_ids, list(set(threads) - set(thread_ids))    
    logging.debug(f"Worker threads pids: {_list_to_str(worker_threads)}")
    logging.debug(f"Additional threads pids: {_list_to_str(additional_threads)}")

    process_splited = process.copy()
    process_splited['threads'] = [main_thread] + additional_threads
    pstree_splited['entries'].append(process_splited)

    next_thread = max(threads) + 1
    duplicated_additional_threads = []

    for thread in worker_threads:
        additional_threads_copy = [next_thread + i for i in range(len(additional_threads))]
        duplicated_additional_threads.append(additional_threads_copy)
        next_thread += len(additional_threads)
 
        process_splited = process.copy()
        process_splited['threads'] = [thread] + additional_threads_copy
        process_splited['pid'] = thread
        process_splited['ppid'] = main_thread
        pstree_splited['entries'].append(process_splited)
    
    save_image(pstree_splited, output_path + "/pstree.img")

    return additional_threads, duplicated_additional_threads

def split_in_core(main_thread, worker_threads, additional_threads, duplicated_additional_threads, input_path, output_path):
    main_thread_core = load_image(input_path + "/core-" + str(main_thread) + ".img")
    assert(main_thread_core['magic'] == 'CORE')

    tc = main_thread_core['entries'][0]['tc']

    for thread in worker_threads:
        thread_core = load_image(input_path + "/core-" + str(thread) + ".img")
        assert(thread_core['magic'] == 'CORE')
        thread_core['entries'][0]['tc'] = tc
        save_image(thread_core, output_path + "/core-" + str(thread) + ".img")
    
    shutil.copyfile(input_path + "/core-" + str(main_thread) + ".img", output_path + "/core-" + str(main_thread) + ".img")

    for additional_thread in additional_threads:
        shutil.copyfile(input_path + "/core-" + str(additional_thread) + ".img", output_path + "/core-" + str(additional_thread) + ".img")

    for additional_threads_copy in duplicated_additional_threads:
        for additional_thread, additional_thread_copy in zip(additional_threads, additional_threads_copy):
            shutil.copyfile(input_path + "/core-" + str(additional_thread) + ".img", output_path + "/core-" + str(additional_thread_copy) + ".img")

def split_in_fs(main_thread, worker_threads, input_path, output_path):
    main_thread_fs = load_image(input_path + "/fs-" + str(main_thread) + ".img")
    assert(main_thread_fs['magic'] == 'FS')

    main_thread_cwd_id = main_thread_fs['entries'][0]['cwd_id']
    main_thread_root_id = main_thread_fs['entries'][0]['root_id']

    logging.debug(f"Main thread cwd id: {main_thread_cwd_id}")
    logging.debug(f"Main thread root id: {main_thread_root_id}")

    next_id = main_thread_root_id + 1

    for thread in worker_threads:
        thread_fs = main_thread_fs.copy()
        thread_fs['entries'][0]['cwd_id'] = next_id
        next_id += 1
        thread_fs['entries'][0]['root_id'] = next_id
        next_id += 1
        save_image(thread_fs, output_path + "/fs-" + str(thread) + ".img")
    
    files = load_image(input_path + "/files.img")
    assert(files['magic'] == 'FILES')
    main_thread_cwd = list(filter(lambda cwd: cwd['id'] == main_thread_cwd_id, files['entries']))[0]
    main_thread_root = list(filter(lambda cwd: cwd['id'] == main_thread_root_id, files['entries']))[0]

    next_id = main_thread_root_id + 1
    for thread in worker_threads:
        thread_cwd = copy.deepcopy(main_thread_cwd)
        thread_cwd['id'] = next_id
        thread_cwd['reg']['id'] = next_id
        next_id += 1
        thread_root = copy.deepcopy(main_thread_root)
        thread_root['id'] = next_id
        thread_root['reg']['id'] = next_id
        next_id += 1
        files['entries'].append(thread_cwd)
        files['entries'].append(thread_root)
    
    save_image(files, output_path + "/files.img")

def duplicate_main_thread_files_for_each_thread(main_thread, worker_threads, input_path, output_path):
    for filename in ["ids-", "mm-", "pagemap-"]:
        for thread in worker_threads:
            shutil.copyfile(input_path + "/" + filename + str(main_thread) + ".img", output_path + "/" + filename + str(thread) + ".img")

def copy_missing_files(input_path, output_path):
    files = [file for file in os.listdir(input_path) if os.path.isfile(os.path.join(input_path, file))]
    for file in files:
        if not os.path.exists(os.path.join(output_path, file)):
            shutil.copy(os.path.join(input_path, file), output_path)

def split(input_path, output_path, thread_ids):
    main_thread, worker_threads = extract_main_thread_pid(get_pids(input_path + "/pstree.img")), thread_ids
    logging.debug(f"Main thread pid: {main_thread}")
    logging.debug(f"Worker threads pids: {_list_to_str(worker_threads)}")

    additional_threads, duplicated_additional_threads = split_in_pstree(input_path, output_path, thread_ids)
    
    split_in_core(main_thread, worker_threads, additional_threads, duplicated_additional_threads, input_path, output_path)

    split_in_fs(main_thread, worker_threads, input_path, output_path)

    duplicate_main_thread_files_for_each_thread(main_thread, worker_threads, input_path, output_path)

    copy_missing_files(input_path, output_path)

def main():
    if len(sys.argv) < 4:
        print("Usage: <input checkpoint path> <output checkpoint path> <program pid>")
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

    thread_ids = split_prepare(pid)
    logging.debug(f"Thread ids: {_list_to_str(thread_ids)}")

    logging.debug("Starting dump")

    result = os.system("criu dump -t " + str(pid) + " -D " + input_path + " -j")
    if result != 0:
        logging.error("Dump failed")
        return
    
    logging.debug("Dump finished")

    logging.debug("Starting split")

    split(input_path, output_path, thread_ids)

    logging.debug("Split finished")

    logging.debug("Starting restore")

    result = os.system("criu restore -D " + output_path + " --restore-detached -j")
    if result != 0:
        logging.error("Restore failed")
        return
    
    logging.debug("Restore finished")
    
    split_finish(pid)
    for thread_pid in thread_ids:
        split_finish(thread_pid)

if __name__ == "__main__":
    main()
