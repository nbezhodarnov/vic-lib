import logging
import zmq

def split_prepare(pid):
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

    threads_count = int(socket.recv_string())
    logging.debug("Received threads count: " + str(threads_count))

    thread_ids = []
    for _ in range(threads_count):
        thread_ids.append(int(socket.recv_string()))
        logging.debug("Received thread id: " + str(thread_ids[-1]))

    socket.close()

    return thread_ids
