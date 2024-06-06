"""Video Stream Agent."""

# pylint: disable=import-error
import os
import logger
import base

# Initialize logger
LOGGER = logger.get_module_logger(__name__)

class VideoAgent(base.BaseAgent):
    