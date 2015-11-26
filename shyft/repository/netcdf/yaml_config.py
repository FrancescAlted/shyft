from __future__ import absolute_import
from __future__ import print_function
from . import region_model
import yaml
from shyft import api
import numpy as np
from datetime import datetime


class YamlContent(object):
    """
    Concrete class for yaml content.
    """

    def __init__(self, config_file):
        self._config_file = config_file
        with open(config_file) as cfg_file:
            config = yaml.load(cfg_file)
        # Expose all keys in yaml file as attributes
        self.__dict__.update(config)

    def __repr__(self):
        srepr = "%s(" % self.__class__.__name__
        for key in self.__dict__:
            srepr += "%s=%r, " % (key, self.__dict__[key])
        srepr = srepr[:-2]
        return srepr + ")"


utc_calendar = api.Calendar()
""" invariant, global calendar, we use utc pr. default, but are still explicit about it"""

def utctime_from_datetime(dt):
    """Returns utctime of datetime dt (calendar interpreted as UTC)"""
    # Number of seconds since epoch
    nsec = np.array([dt], dtype="datetime64[s]").astype(np.long)[0]
    dt = datetime.fromtimestamp(nsec)
    return utc_calendar.time(api.YMDhms(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second))


class ConfigContent(object):
    """
    Concrete class for yaml content.
    """

    def __init__(self, config_file, config_section):
        self._config_file = config_file
        self._config_section = config_section
        with open(config_file) as cfg_file:
            config = yaml.load(cfg_file)[config_section]
        # Expose all keys in yaml file as attributes
        self.__dict__.update(config)
        # Create a time axis
        self.start_time = utctime_from_datetime(self.start_datetime)
        self.time_axis = api.Timeaxis(
            self.start_time, self.run_time_step, self.number_of_steps)

    def __repr__(self):
        srepr = "%s::%s(" % (self.__class__.__name__, self._config_section)
        for key in self.__dict__:
            srepr += "%s=%r, " % (key, self.__dict__[key])
        srepr = srepr[:-2]
        return srepr + ")"


class RegionConfig(region_model.RegionConfig):
    """
    Yaml based region configuration, using a YamlContent instance
    for holding the content.
    """

    def __init__(self, config_file):
        self._config = YamlContent(config_file)

    def parameter_overrides(self):
        return getattr(self._config, "parameter_overrides", {})

    def domain(self):
        return self._config.domain

    def repository(self):
        return self._config.repository


class ModelConfig(region_model.ModelConfig):
    """
    Yaml based model configuration, using a YamlContent instance
    for holding the content.
    """

    def __init__(self, config_file):
        self._config = YamlContent(config_file)

    def interpolation_parameters(self):
        return self._config.parameters["interpolation"]

    def model_parameters(self):
        return self._config.parameters["model"]
