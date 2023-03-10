#
# asyn documentation

import sys, os

project = 'asyn'
html_title = html_short_title = 'asyn support'

authors = 'Mark Rivers'
copyright = '2023, Mark Rivers'

extensions = ['sphinx.ext.autodoc',
              'sphinx.ext.mathjax',
              'sphinx.ext.extlinks',
              'sphinx.ext.napoleon']

todo_include_todos = True

templates_path = ['_templates']
source_suffix = '.rst'
source_encoding = 'utf-8'
master_doc = 'index'
today_fmt = '%Y-%B-%d'

exclude_trees = ['_build']

add_function_parentheses = True
add_module_names = False
pygments_style = 'sphinx'

# html themes: 'default', 'sphinxdoc',  'alabaster', 'agogo', 'nature', 'pyramid'
#html_theme = 'pyramid'
html_theme = 'sphinx_rtd_theme'

html_static_path = ['_static']
html_style = 'css/my_theme.css'
html_last_updated_fmt = '%Y-%B-%d'
html_show_sourcelink = True
htmlhelp_basename = 'asyn_doc'
