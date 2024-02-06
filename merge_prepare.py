import logging
import zmq
import time

def merge_prepare_worker(pid):
    address = "ipc:///tmp/vic_transform_prepare_" + str(pid)

    context = zmq.Context()
    
    socket = context.socket(zmq.DEALER)
    socket.connect(address)

    logging.debug("Successfully connected to " + address)
    
    socket.send_string("prepare")
    logging.debug("Sent prepare message")

    socket.close()

    socket = context.socket(zmq.DEALER)
    socket.connect(address)

    logging.debug("Successfully connected 2nd time to " + address)

    ready_message = socket.recv_string()
    logging.debug("Received ready message: " + ready_message)

    socket.close()

    return ready_message == "ready"

def merge_prepare_main(pid, max_stack_size):
    address = "ipc:///tmp/vic_transform_prepare_" + str(pid)

    context = zmq.Context()
    
    socket = context.socket(zmq.DEALER)
    socket.connect(address)

    logging.debug("Successfully connected to " + address)
    
    socket.send_string("prepare")
    logging.debug("Sent prepare message")

    socket.close()

    socket = context.socket(zmq.DEALER)
    socket.connect(address)

    logging.debug("Successfully connected 2nd time to " + address)

    time.sleep(1)

    socket.send_string(str(max_stack_size))
    logging.debug("Sent max stack size: " + str(max_stack_size))

    ready_message = socket.recv_string()
    logging.debug("Received ready message: " + ready_message)

    threads_count = int(socket.recv_string())
    logging.debug("Received threads count: " + str(threads_count))

    processes_threads_relationship = []
    for _ in range(threads_count):
        pid_thread_dict = {}

        splited_message = socket.recv_string().split(" ")
        pid_thread_dict["pid"] = int(splited_message[0])
        pid_thread_dict["thread_id"] = int(splited_message[1])

        processes_threads_relationship.append(pid_thread_dict)
        logging.debug("Received: pid - " + str(processes_threads_relationship[-1]["pid"]) \
                      + ", thread_id - " + str(processes_threads_relationship[-1]["thread_id"]))

    socket.close()

    return processes_threads_relationship
