import logging
import zmq

def merge_finish(pid):
    address = "ipc:///tmp/vic_transform_prepare_" + str(pid)
    context = zmq.Context()
    
    socket = context.socket(zmq.DEALER)
    socket.connect(address)

    logging.debug("Successfully connected to " + address)
    
    socket.send_string("start")
    logging.debug("Sent start message to pid " + str(pid))

    socket.close()
