#!/usr/bin/env python
from waflib.extras import autowaf as autowaf
import re

# Variables for 'waf dist'
APPNAME = 'sm.lv2'
VERSION = '0.0.1'

# Mandatory variables
top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_c')
    opt.load('lv2')
    autowaf.set_options(opt)

def configure(conf):
    conf.load('compiler_c', cache=True)
    conf.load('lv2', cache=True)
    conf.load('autowaf', cache=True)

    autowaf.check_pkg(conf, 'lv2', uselib_store='LV2')

    conf.check(features='c cshlib', lib='m', uselib_store='M', mandatory=False)

    autowaf.display_msg(conf, 'LV2 bundle directory', conf.env.LV2DIR)
    print('')

def build(bld):
    bundle = 'sm.lv2'

    # Make a pattern for shared objects without the 'lib' prefix
    module_pat = re.sub('^lib', '', bld.env.cshlib_PATTERN)
    module_ext = module_pat[module_pat.rfind('.'):]

    # Build manifest.ttl by substitution (for portable lib extension)
    bld(features     = 'subst',
        source       = 'manifest.ttl.in',
        target       = '%s/%s' % (bundle, 'manifest.ttl'),
        install_path = '${LV2DIR}/%s' % bundle,
        LIB_EXT      = module_ext)

    # Copy other data files to build bundle (build/sm.lv2)
    for i in ['sm.ttl']:
        bld(features     = 'subst',
            is_copy      = True,
            source       = i,
            target       = '%s/%s' % (bundle, i),
            install_path = '${LV2DIR}/%s' % bundle)

    # Use LV2 headers from parent directory if building as a sub-project
    # Build plugin library
    obj = bld(features     = 'c cshlib',
              source       = 'sm.c',
              name         = 'sm',
              target       = '%s/sm' % bundle,
              install_path = '${LV2DIR}/%s' % bundle,
              uselib       = 'M LV2')
    obj.env.cshlib_PATTERN = module_pat
