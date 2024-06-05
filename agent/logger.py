"""Logger module"""

# pylint: disable=import-error
import logging
import logging.handlers
import os


def get_module_logger(mod_name: str) -> logging.Logger:
    """Create module logger

    :param mod_name: module name
    :return: logger
    :rtype: logging.Logger
    """

    logger = logging.getLogger(mod_name)
    logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        "%(asctime)s | %(levelname)s | %(filename)s:%(lineno)d | %(message)s"
    )

    log_folder = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")

    if not os.path.exists(log_folder):
        os.makedirs(log_folder)

    file_handler = logging.FileHandler(os.path.join(log_folder, "agent.log"))
    file_handler.setFormatter(formatter)
    logger.addHandler(file_handler)

    stream_handler = logging.StreamHandler()
    stream_handler.setFormatter(formatter)
    logger.addHandler(stream_handler)

    return logger
