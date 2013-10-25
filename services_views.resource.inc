<?php
/**
 * @file
 * Callbacks for services module resource hooks.
 */

/**
 * Callback for retrieving views resources.
 *
 * @param $view_name
 *   String. The views name.
 * @param $display_id
 *   String (optional). The views display name.
 * @param $args
 *   Array (optional). A list of arguments to pass to the view.
 * @param $offset
 *   Integer (optional). An offset integer for paging.
 * @param $limit
 *   Integer (optional). A limit integer for paging.
 * @param $return_type
 *   String (optional). Whether to return the raw data results (FALSE), the entire views object ('view') or themed results ('theme').
 * @param $filters
 *   Array (optional). A list of exposed filters to pass to the view.
 *
 * @return
 *  Array. The views return.
 */
function services_views_retrieve($view_name, $display_id = 'default', $args = array(), $offset = 0, $limit = -1, $return_type = FALSE, $filters = array()) {
  $result = array();
  $view = views_get_view($view_name);
  $view->get_total_rows = TRUE;
  // set to allow _total_rows stats in result

  // Put all arguments and filters and then execute.
  $view->set_arguments($args, FALSE);
  $view->set_exposed_input($filters);
  $view->set_offset($offset);

  // Support for Views 2
  if (method_exists($view, 'set_use_pager')) {
    // If offset is set we can't have a user pager.
    if (empty($offset)) {
      $view->set_use_pager(TRUE);
      if ($limit >= 0) {
        $view->set_items_per_page($limit);
      }
    } else {
      // Disable the user pager.
      $view->set_use_pager(FALSE);
    }
  } elseif ($limit >= 0) {
    $view->set_items_per_page($limit);
  }

  // If we display values and not themed view output.
  if (empty($return_type)) {
    $view->set_display($display_id);
    // pre-execute to get row-counts
    $view->pre_execute();
    $view->execute();
    $result = services_views_render_output($view);

  } elseif ($return_type == 'view') {
    $view->set_display($display_id);
    $view->execute();
    return $view;
  } else {
    // We want to keep the result an array.
    $result[] = $view->preview($display_id);
  }
  drupal_alter('services_views_execute_view', $result, $view);

  return $result;
}

/*
 * Public helper which can be used by other callers
 * Specifically it's called by the entityreference_views_formatter.module
 */
function services_views_render_output($view) {
  $result = $view->result;

  $row_plugin = $view->display_handler->get_option('row_plugin');
  // If row plugin is node, then we should load each node
  if ($row_plugin == 'node') {
    $nodes = array();
    foreach ($view->result as $row) {
      $nodes[] = node_load($row->nid);
    }
    $result = $nodes;
  } elseif ($row_plugin == 'fields') {
    $items = array();
    foreach ($view->field as $field) {
      $field->pre_render($view->result);
    }
    foreach ($view->result as $row_index => $row) {
      $item = new stdClass();
      foreach ($view->field as $field_id => $field) {
        if (!empty($field->options['label'])) {
          $field_label = strtolower($field->options['label']);
        } elseif ($field->field_alias) {
          $field_label = $field->field_alias;
        } else {
          $field_label = $field_id;
        }
        $field->view->row_index = $row_index;
        // We want to retain lists as lists
        $fval = _services_views_get_field_value($row, $field, $field_id, $row_index);
        $item->$field_label = $fval;
      }
      $items[] = $item;
    }
    if ($items) {
      $result = $items;
    }
  }

  return $result;
}

function _services_views_get_field_value($row, $field, $field_key, $index) {
  if (in_array($field_key, array('nid', 'title', 'body'))) {
    // There's no need to format the value. just return it as-is
    return $field->theme($row);
  }
  $f_key = $f_key = 'field_' . $field_key;
  //
  $entity_property = FALSE;
  if (!isset($row->$f_key)) {
    $f_key = $field->field_alias;
    $entity_property = TRUE;
  }
  if (!isset($row->$f_key)) {
    return;
  }
  $target_key = $field_key;
  // $format = ($field->options['type'] == 'services') ? 'raw' : 'rendered';
  // if field->options['type'] == 'entityreference_entity_id'
  // if field->options['type'] == ???

  // Skip this field?
  if (isset($field->options['exclude']) && $field->options['exclude'] == TRUE) {
    return;
  }
  // Use the label defined in the view
  if (isset($field->options['label']) && !empty($field->options['label'])) {
    $target_key = $field->options['label'];
  }

  // Move key if we have an entity property and the target_key was changed via label
  if ($entity_property) {
    return $field->theme($row);
  }

  $obj = array();

  $format = ($field->options['type'] == 'services') ? 'raw' : 'rendered';

  foreach ($row->$f_key as $idx => $res) {
    if (!isset($res[$format])) {
      continue;
    }

    // We now have data and a format
    $data = $res[$format];

    // If this is raw formatter.
    if ($format == 'raw') {
      foreach ($data as $key => $val) {
        if ($field->options['settings']['skip_safe'] && strpos($key, 'safe_') === 0) {
          unset($data[$key]);
        }
        if ($field->options['settings']['skip_empty_values'] && empty($val)) {
          unset($data[$key]);
        }
      }
      if (empty($data)) {
        unset($obj[$idx]);
      } else {
        $obj[$idx] = $data;
      }
    }
    // $format == rendered
    elseif (isset($data['#access']) && $data['#access'] == TRUE) {
      if ($field->multiple == true) {
        // If there's no theme hook, return the #markup text
        if (!isset($res['rendered']['#theme'])) {
          if (isset($res['rendered']['#markup'])) {
            $rdata = $res['rendered']['#markup'];
          } else {
            // Weirness - we've got a field value with no #markup, and no #theme!!!
            watchdog('services_views', 'Field has no markup or theme: ' . $field->field, array(), WATCHDOG_ERROR, null);
            $rdata = null;
          }
        } else {
          $rdata = render($res);
          // $rdata = $field->theme($row);
        }
      } else {
        $rdata = $field->theme($row);
      }
      if (empty($rdata)) {
        unset($obj[$idx]);
      } else {
        $obj[$idx] = $rdata;
      }
    } else {
      $obj[$idx] = array('error' => t('Access denied or format unknown on field.'));
    }
  }
  // End for-loop
  if (!$field->multiple && is_array($obj) && sizeof($obj) == 1) {
    // flatten the array since we don't want lists all the time
    $retval = $obj[0];
  } else {
    $retval = $obj;
  }
  // Don't return empty arrays
  if (is_array($retval) && sizeof($retval) == 0) {
    $retval = null;
  }
  return $retval;
}

/**
 * Execute a view and return results.
 */
function services_views_execute_view($view_info, $view = NULL, $display_id = NULL) {
  if (!empty($view_info)) {
    $view_name = $view_info['view_name'];
    $display_id = $view_info['display_id'];
    $args = $view_info['args'];

    $view = views_get_view($view_name);
    $view->set_arguments($args);
    $view->execute_display($display_id, $args);
    $view->get_total_rows = TRUE;
  }

  // Avoid translation of the field labels.
  $view->localization_plugin = views_get_plugin('localization', 'none');

  // Execute a view.
  $output = $view->render($display_id);

  // Add support for Services Raw formatter.
  foreach ($view->result as $index => $row) {
    foreach ($view->field as $field_key => $field) {
      $f_key = 'field_' . $field_key;
      $entity_property = FALSE;
      if (!isset($row->$f_key)) {
        $f_key = $field->field_alias;
        $entity_property = TRUE;
      }
      if (isset($row->$f_key)) {
        $target_key = $field_key;
        $format = "rendered";
        if (in_array('type', $field->options)) {
          $format = ($field->options['type'] == 'services') ? 'raw' : 'rendered';
        }

        // Skip this field?
        if (isset($field->options['exclude']) && $field->options['exclude'] == TRUE) {
          continue;
        }

        if (isset($field->options['label']) && !empty($field->options['label'])) {
          $target_key = $field->options['label'];
        }

        // Move key if we have an entity property and the target_key was changed via label
        if ($entity_property) {
          unset($output[$index]->$f_key);
          $output[$index]->$target_key = $field->theme($row);
          // @todo: Do not render twice; use view->execute_display and consolidate functions
          continue;
        }

        // Create helper variables
        $output[$index]->$target_key = array();
        $obj = &$output[$index]->$target_key;

        foreach ($row->$f_key as $idx => $res) {

          if (isset($res[$format])) {
            $data = $res[$format];

            // If this is raw formatter.
            if ($format == 'raw') {
              foreach ($data as $key => $val) {
                if ($field->options['settings']['skip_safe'] && strpos($key, 'safe_') === 0) {
                  unset($data[$key]);
                }
                if ($field->options['settings']['skip_empty_values'] && empty($val)) {
                  unset($data[$key]);
                }
              }
              if (empty($data)) {
                unset($obj[$idx]);
              } else {
                $obj[$idx] = $data;
              }
            }
            // $format == rendered
            elseif (isset($data['#access']) && $data['#access'] == TRUE) {
              if ($field->multiple == true) {
                // If there's no theme hook, return the #markup text
                if (!isset($res['rendered']['#theme'])) {
                  if (isset($res['rendered']['#markup'])) {
                    $rdata = $res['rendered']['#markup'];
                  } else {
                    // Weirness - we've got a field value with no #markup, and no #theme!!!
                    watchdog('services_views', 'Field has no markup or theme: ' . $field->field, array(), WATCHDOG_ERROR, null);
                    $rdata = null;
                  }
                } else {
                  $rdata = render($res);
                  // $rdata = $field->theme($row);
                }
              } else {
                $rdata = $field->theme($row);
              }
              if (empty($rdata)) {
                unset($obj[$idx]);
              } else {
                $obj[$idx] = $rdata;
              }
            } else {
              $obj[$idx] = array('error' => t('Access denied or format unknown on field.'));
            }
          }
        }

        // Check html_strip property
        if ($field->options['alter']['strip_tags'] == '1') {
          // Strip the tags which aren't allowed using strip_tags ( $subject, $preserved_tags ).
          $obj[$idx] = strip_tags($obj[$idx], $field->options['alter']['preserve_tags']);
        }

        // Check cardinality of the field.
        if ($field->field_info['cardinality'] == 1 && count($obj) == 1) {
          $obj = $obj[$idx];
        }

        // Don't return blank values. However, this may break some front-ends
        //  if they're expecting all fields to be returned.
        // if ($obj == null) {
        //   unset($output[$index]->$target_key);
        // }
      }
    }
  }

  drupal_alter('services_views_execute_view', $output, $view);

  $view->destroy();
  return $output;
}
