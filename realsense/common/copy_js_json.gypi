# Copyright (c) 2015 Intel Corporation. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'actions': [
    {
      'action_name': 'copy_js_json_<(_target_name)',
      'message': 'Copying js and json of <(_target_name)',
      'inputs': [
        '<(js_file)',
        '<(json_file)',
        '<(compile_stamp)',
      ],
      'outputs': [
        '<(extension_dir)/<(js_file)',
        '<(extension_dir)/<(json_file)',
      ],
      'action': [
        'cp',
        '<(js_file)',
        '<(json_file)',
        '<(extension_dir)',
      ]
    },
  ],
}
