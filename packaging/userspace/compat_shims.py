#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Compatibility shims for some python stdlib stuff."""

from __future__ import print_function
import sys, errno  # noqa: I201; pylint: disable=C0410

if __name__ == '__main__':
	print(__file__ + ': This module is not meant to be invoked directly.',  # noqa: T001
	      file=sys.stderr)
	sys.exit(errno.ELIBEXEC)

import importlib  # noqa: I100,I202
import platform
import site
from threading import Lock

import pkg_resources

site_import_paths = list(site.getsitepackages())
if site.ENABLE_USER_SITE:
	site_import_paths.insert(0, site.getusersitepackages())

Version = type(pkg_resources.Distribution(version='0.0.0').parsed_version)

_str_ver_cache = {}
_str_ver_cache_lock = Lock()
_inf_ver_cache = {}
_inf_ver_cache_lock = Lock()


def parse_version(ver_str: str) -> Version:
	if ver_str in _str_ver_cache:
		return _str_ver_cache[ver_str]
	with _str_ver_cache_lock:
		# check one more time in case it was added while waiting on the lock
		if ver_str in _str_ver_cache:
			return _str_ver_cache[ver_str]
		ver = Version(ver_str)
		_str_ver_cache[ver_str] = ver
		return ver


def version_info_to_version(
	major: int = 0,
	minor: int = 0,
	micro: int = 0,
	releaselevel: str = 'final',
	serial: int = 0
) -> Version:
	version_info = (major, minor, micro, releaselevel, serial)
	if version_info in _inf_ver_cache:
		return _inf_ver_cache[version_info]
	with _inf_ver_cache_lock:
		# check one more time in case it was added while waiting on the lock
		if version_info in _inf_ver_cache:
			return _inf_ver_cache[version_info]
		if releaselevel == 'final':
			pre_fmt = ''
		elif releaselevel == 'alpha':
			pre_fmt = 'a{3}'
		elif releaselevel == 'beta':
			pre_fmt = 'b{3}'
		elif releaselevel == 'candidate':
			pre_fmt = 'rc{3}'
		else:
			raise ValueError('unrecognized releaselevel value')
		ver_fmt = '{0}.{1}.{2}' + pre_fmt
		ver_str = ver_fmt.format(major, minor, micro, serial)
		ver = parse_version(ver_str)
		_inf_ver_cache[version_info] = ver
		return ver


try:
	python_version = version_info_to_version(
		sys.version_info.major,
		sys.version_info.minor,
		sys.version_info.micro,
		sys.version_info.releaselevel,
		sys.version_info.serial
	)
except Exception:  # noqa: B902,S110; pylint: disable=W0703; nosec
	python_version = parse_version(platform.python_version())

# There is an infinite recursion bug in typing < 3.5.3.
# If there's a newer version of typing installed, use it.
_req_typing_stdlib_ver = version_info_to_version(3, 5, 3)
typing_version = python_version
if python_version < _req_typing_stdlib_ver:
	try:
		typing_dist = pkg_resources.get_distribution('typing>=3.5.3')
	except pkg_resources.ResolutionError:
		pass
	else:
		# Just to be safe, let's save/restore the whole list instead of just
		# removing the entry we insert
		old_path = list(sys.path)
		sys.path.insert(1, typing_dist.location)

		try:
			importlib.invalidate_caches()
			import typing
		except ImportError:
			pass
		else:
			sys.modules['typing'] = typing
		finally:
			# On the off chacne there are cached references to sys.path somewhere, edit it in-place
			sys.path.clear()
			sys.path.extend(old_path)

# pylint: disable=C0412
import collections  # noqa: I100,I202
import typing

if sys.version_info < (3, 9):
	from typing import ByteString, Callable, Container, Generator, Iterable, Iterator
	from typing import MutableSequence, MutableSet, Reversible, Sequence
	from typing import ItemsView, KeysView, Mapping, MappingView, MutableMapping, ValuesView
else:
	from collections.abc import ByteString, Callable, Container, Generator, Iterable, Iterator
	from collections.abc import MutableSequence, MutableSet, Reversible, Sequence
	from collections.abc import ItemsView, KeysView, Mapping, MappingView, MutableMapping, ValuesView

# Utility stuff
if sys.version_info < (3, 6):
	def _check_methods(cls, *methods):  # noqa: ANN001,ANN002,ANN202
		mro = cls.__mro__
		for method in methods:
			for scls in mro:
				if method in scls.__dict__:
					if scls.__dict__[method] is None:
						return NotImplemented
					break
			else:
				return NotImplemented
		return True
if typing_version < version_info_to_version(3, 7):
	try:
		from typing import _generic_new
	except ImportError:
		from typing import _gorg
		def _generic_new(base_cls, cls, *args, **kwds):  # noqa: ANN001,ANN002,ANN003,ANN202
			if cls.__origin__ is None:
				return base_cls.__new__(cls)
			else:
				origin = _gorg(cls)
				obj = base_cls.__new__(origin)
				try:
					obj.__orig_class__ = cls
				except AttributeError:
					pass
				obj.__init__(*args, **kwds)
				return obj
	try:
		from typing import _geqv
	except ImportError:
		def _geqv(cls1, cls2):  # noqa: ANN001,ANN202
			return cls1._gorg is cls2  # pylint: disable=W0212
if (3, 7) <= sys.version_info < (3, 9):
	from typing import _alias
if sys.version_info < (3, 9):
	from typing import KT, T, T_co, VT

# @overload decorator broken in 3.5.0 and 3.5.1
if typing_version < version_info_to_version(3, 5, 2):
	def _overload_dummy(*args, **kwargs):
		raise NotImplementedError('call to overloaded function')
	def overload(fn):  # noqa: ANN001,ANN201; pylint: disable=W0613
		return _overload_dummy
else:
	from typing import overload

# NoReturn introduced in 3.5.4 and 3.6.2
try:
	from typing import NoReturn
except ImportError:
	NoReturn = type(None)

# Type was introduced in 3.5.2 and deprecated in 3.9
# However, Type was broken in 3.5.2 and fixed in 3.5.3
if typing_version < _req_typing_stdlib_ver:
	# We can use CT from 3.5.2; however, Xenial's Python may be 3.5.1 masquerading as 3.5.2,
	# so do a try except
	try:
		from typing import CT as CT_co
	except ImportError:
		CT_co = typing.TypeVar('CT_co', covariant=True, bound=type)  # pylint: disable=C0103
	class Type(typing.Generic[CT_co], extra=type):
		__slots__ = ()
elif sys.version_info < (3, 9):
	from typing import Type
else:
	Type = type

# ChainMap and Counter were introduced in 3.5.4 and 3.6.1, and deprecated in 3.9.
if (
	(sys.version_info < (3, 5, 4) and typing_version < version_info_to_version(3, 6, 1)) or
	(3, 6) <= sys.version_info < (3, 6, 1)
):
	class ChainMap(collections.ChainMap, MutableMapping[KT, VT], extra=collections.ChainMap):
		__slots__ = ()
		def __new__(cls, *args, **kwargs):  # noqa: ANN002,ANN003,ANN204
			if _geqv(cls, ChainMap):
				return collections.ChainMap(*args, **kwargs)
			return _generic_new(collections.ChainMap, cls, *args, **kwargs)
	class Counter(collections.Counter, typing.Dict[T, int], extra=collections.Counter):  # pylint: disable=W0223
		__slots__ = ()
		def __new__(cls, *args, **kwargs):  # noqa: ANN002,ANN003,ANN204
			if _geqv(cls, Counter):
				return collections.Counter(*args, **kwargs)
			return _generic_new(collections.Counter, cls, *args, **kwargs)
elif sys.version_info < (3, 9):
	from typing import ChainMap, Counter
else:
	from collections import ChainMap, Counter

# DefaultDict was introduced in 3.5.2 and deprecated in 3.9
# However, it was not actually usable until 3.5.4
if typing_version < version_info_to_version(3, 5, 4):
	class DefaultDict(collections.defaultdict, MutableMapping[KT, VT], extra=collections.defaultdict):
		__slots__ = ()
		def __new__(cls, *args, **kwargs):  # noqa: ANN002,ANN003,ANN204
			if _geqv(cls, DefaultDict):
				return collections.defaultdict(*args, **kwargs)
			return _generic_new(collections.defaultdict, cls, *args, **kwargs)
elif sys.version_info < (3, 9):
	from typing import DefaultDict
else:
	from collections import defaultdict as DefaultDict  # noqa: N812

# Collection was introduced in 3.6 and deprecated in 3.9
if sys.version_info < (3, 6):
	class Collection(collections.abc.Sized, Iterable[T_co], Container[T_co]):  # pylint: disable=W0223
		__slots__ = ()
		@classmethod
		def __subclasshook__(cls, subclass):  # noqa: ANN001,ANN206; pylint: disable=W0221
			if cls is Collection:
				return _check_methods(subclass, '__len__', '__iter__', '__contains__')
			return NotImplemented
elif sys.version_info < (3, 9):
	from typing import Collection
else:
	from collections.abc import Collection

# OrderedDict was introduced in 3.7.2 and deprecated in 3.9
if sys.version_info < (3, 7):
	class OrderedDict(collections.OrderedDict, MutableMapping[KT, VT], extra=collections.OrderedDict):
		__slots__ = ()
		def __new__(cls, *args, **kwargs):  # noqa: ANN002,ANN003,ANN204
			if _geqv(cls, OrderedDict):
				return collections.OrderedDict(*args, **kwargs)
			return _generic_new(collections.OrderedDict, cls, *args, **kwargs)
elif sys.version_info < (3, 7, 2):
	# For whatever reason, pylint can crash if we access collections.OrderedDict directly here
	from collections import OrderedDict as OrderedDict_collections
	OrderedDict = _alias(OrderedDict_collections, (KT, VT))
elif sys.version_info < (3, 9):
	# Whatever Wing uses to underline problematic code thinks typing.OrderedDict doesn't exist
	OrderedDict = typing.OrderedDict  # noqa: TYP006
else:
	OrderedDict = collections.OrderedDict

# Protocol was introduced in 3.8.
# _Protocol is an internal type we can use as a shim for < 3.8
if typing_version < version_info_to_version(3, 8):
	from typing import _Protocol as Protocol  # pylint: disable=E0611
else:
	from typing import Protocol

# AbstractSet -> Set
# tuple -> Tuple
if sys.version_info < (3, 9):
	from typing import AbstractSet as Set
	from typing import Tuple
else:
	from collections.abc import Set
	Tuple = tuple

# platform.linux_distribution was deprecated in 3.5 and removed in 3.8
# the distro package is the recommended replacement
try:
	from distro import linux_distribution
except ImportError:
	if sys.version_info < (3, 8):
		from platform import linux_distribution
	else:
		raise

__all__ = [
	'Version',
	'parse_version',
	'version_info_to_version',
	'python_version',
	'typing_version',
	'ByteString',
	'Callable',
	'Generator',
	'Iterable',
	'Iterator',
	'MutableSequence',
	'MutableSet',
	'Sequence',
	'ItemsView',
	'KeysView',
	'Mapping',
	'MappingView',
	'MutableMapping',
	'ValuesView',
	'overload',
	'NoReturn',
	'DefaultDict',
	'Type',
	'Collection',
	'Reversible',
	'OrderedDict',
	'Protocol',
	'Set',
	'Tuple',
	'linux_distribution'
]
