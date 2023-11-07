import epics

from . import conftest


def test_basic(standard_env: conftest.EnvironmentInfo):
    pv = epics.get_pv("ioc:auto:cnt")
    assert pv.get() == 0.0
