<?php

/**
 * @file
 *
 * Utility functions that provided simplicity or convenience.
 */

/**
 * Given an object info array as defined by the *_info() functions, determine if the user can access it.
 *
 * @param array $object_info
 *   An array containing a 'pid' field.
 *
 * @return boolean
 *  TRUE if accessible FALSE otherwise.
 */
function si_exhibition_check_object_info_accessible(array $object_info) {
  //BBB Until we are different about permissions, assume the user can access it
  return TRUE;
  $repo = tuque_wrapper_get_repository_instance();
  si_exhibition_debug('repo from si_exhibition_check_object_info_accessible');
  try {
    $object = $repo->getObject($object_info['pid']);
    // @todo remove object from cache.
  } catch(Exception $e) {
    return FALSE;
  }
  return TRUE;
}
/*
* For exhibitions, hides parents that aren't supposed to be shown, only the parents that should be in the current exhibition path
*
*/
function si_exhibition_check_parent_in_tree(array $object_info, $tree_pid) {
  $tree_pid_list = array();
  $tree = _si_exhibition_get_pid_tree($tree_pid, null, $tree_pid_list);
  return in_array($object_info['pid'], $tree_pid_list);
}


/**
 * Fetchs some information about the given object's parents.
 *
 * @param FedoraObject $object
 *   The object who's parents info will be returned.
 *
 * @return array
 *   An array of array's containing the following key value pairs for each parent of the given object:
 *    pid, label
 */
function si_exhibition_get_parent_info(FedoraObject $object, $skipNonTreeParents = false) {
  $repo = tuque_wrapper_get_repository_instance();
  si_exhibition_debug('repo from si_exhibition_get_parent_info');
  $query = si_exhibition_generate_parent_concept_query($object->id);
  $results = $repo->ri->itqlQuery($query);
  $parents = array_map(function($o) {
    return array('pid' => $o['o']['value'], 'label' => $o['t']['value']); 
  }, $results);
  
  // @todo replace the checks for each objects existance once we have the appropriate information in the rels to determine if its accessible.
  if ($skipNonTreeParents) {
    $parents = array_filter($parents, function($elem) use ($object){
      $path = (isset($_GET['path']) ? $_GET['path'] : '');
      if (!$path) {
        return false;
      }
      $paths = explode(',',$path);
      return si_exhibition_check_parent_in_tree($elem, $paths[0]);
    });
    if (variable_get('si_exhibition_anonymous_only', FALSE)){
      $parents = array_filter($parents, function($elem){
        return si_exhibition_check_view_permission($elem['pid']);
      });
    }
  }
  
  return array_filter($parents, 'si_exhibition_check_object_info_accessible');
}

/**
 * Query for fetching the Parent Concepts pids and labels.
 *
 * @param string $pid
 *   The object to query for.
 *
 * @return string
 *   An itql query string.
 */
function si_exhibition_generate_parent_concept_query($pid) {
  return 'select $o $t from <#ri> where
    $o <fedora-model:state> <fedora-model:Active> and
    $o <fedora-model:label> $t and
    $o <fedora-rels-ext:hasConcept> <info:fedora/' . $pid . '>
    order by $t';
}


/**
 * Fetchs some information about the given object's child concepts.
 *
 * @param FedoraObject $object
 *   The parent object who's child concept info will be returned.
 *
 * @return array
 *   An array of array's containing the following key value pairs for each child concpet of the given object:
 *    pid, label
 */
function si_exhibition_get_child_concept_info(FedoraObject $object) {
  return si_exhibition_get_child_concept_info_by_id($object->id);
}
function si_exhibition_get_child_concept_info_by_id($object_id) {
  //Start cache check for return
  $cache_name = 'child_concept_info';
  $cache_index = $object_id;
  $to_return = si_exhibition_cache($cache_name,$cache_index);
  if (!empty($to_return)){ return $to_return; }
  si_exhibition_cron_cache(__FUNCTION__, func_get_args());
  //End cache

  $repo = tuque_wrapper_get_repository_instance();
  si_exhibition_debug('repo from si_exhibition_get_child_concept_info');
  $query = si_exhibition_generate_child_concept_query($object_id);
  $results = $repo->ri->itqlQuery($query);
  $children = array_map(function($o) { return array('pid' => $o['o']['value'], 'label' => $o['t']['value'], 'count' => $o['k0']['value']); }, $results);
  // @todo replace the checks for each objects existance once we have the appropriate information in the rels to determine if its accessible.
  if (variable_get('si_exhibition_anonymous_only', FALSE)){
    $children = array_filter($children, function($elem){
      return si_exhibition_check_view_permission($elem['pid']);
    });
  }
  //var_dump ($children);
  $to_return = array_filter($children, 'si_exhibition_check_object_info_accessible');
  //var_dump($to_return);
  return si_exhibition_cache($cache_name, $cache_index, $to_return);
}

/**
 * Query for fetching the Child Concepts labels and the number of Child Concepts they have.
 *
 * @param string $pid
 *   The object to query for.
 *
 * @return string
 *   An itql query string.
 */
function si_exhibition_generate_child_concept_query($pid) {
  $count = 'count(select $c from <#ri> where
    $o <fedora-rels-ext:hasConcept> $c and
    $c <fedora-model:state> <fedora-model:Active>)';
  return 'select $o $t ' . $count . ' from <#ri> where
     $o <fedora-model:state> <fedora-model:Active> and
     $o <fedora-model:label> $t and
     <info:fedora/' . $pid . '> <fedora-rels-ext:hasConcept> $o
     order by $t';
}
/*
 * Enable verbose levels of debugging
 */
function si_exhibition_debug($message, $object = NULL){
  if (variable_get('si_exhibition_debug',FALSE)){
    drupal_set_message($message);
    if (!empty($object)){
      ob_start();
      print '<div id="" style="overflow-y: scroll; max-height:400px;">';
      print '<pre>';
      var_dump ($object);
      print '</pre>';
      print '</div>';
      $content = ob_get_clean();
      drupal_set_message($content);
    }
  }
}

/*
 * Returns the data from the cache if called with a cache_name and a cache_id
 * Returns the entire cached array if called with a cache_name, empty string and no third param
 * Returns the "overwrite" parameter if it has a value
 * Saves data in the cache if "overwrite" parameter has a value
 * If permanent set to true, will not expire
 */
function si_exhibition_cache($cache_name, $cache_data_index, $overwrite = FALSE, $permanent = FALSE){
  global $tnum;
  si_exhibition_debug("si_exhibition_cache $cache_name | $cache_data_index | $overwrite | $permanent |");
  if (!variable_get('si_exhibition_cache_on',TRUE)){
    si_exhibition_debug("si_exhibition_cache is turned off");
    if ($overwrite !== FALSE) return $overwrite; //If caching is turned off, don't use caching!
    return NULL;
  }
  $prefix = 'si_exhibition_cache_';
  $cache_table_for_clearing = 'cache';
  if ($overwrite === FALSE){
    $cache = NULL;
    if ($cache_info = cache_get($prefix.$cache_name, $cache_table_for_clearing)){
      $cache = $cache_info->data;
      if ($cache_data_index == ''){
        si_exhibition_debug("si_exhibition_cache returning entire cache", $cache);
        //return the entire cache
        return $cache;
      }
      if (array_key_exists($cache_data_index, $cache)){
        si_exhibition_debug("si_exhibition_cache returning specific cache", $cache[$cache_data_index]);
        return $cache[$cache_data_index];
      }
    }
  }else{
    /*
    if (!is_string($overwrite)){
      if (empty($tnum)) $tnum = 0;
      $tnum++;
      print 'NOT STRING wbt';
      var_dump($overwrite);
      debug_print_backtrace();
      if ($tnum > 1) exit();
    }
    */
    $data = array();
    if ($cache_info = cache_get($prefix.$cache_name, $cache_table_for_clearing)){
      $data = $cache_info->data;
    }
    $data[$cache_data_index] = $overwrite;
    $hours = 3;
    $expire_date_time = time() + 60 * 60 * $hours;
    if ($permanent === TRUE){
      $expire_date_time = CACHE_PERMANENT;
    }
    si_exhibition_debug("si_exhibition_cache setting cache $prefix | $cache_name", $data);
    cache_set($prefix.$cache_name, $data, $cache_table_for_clearing, $expire_date_time);
    return $overwrite;
  }
  si_exhibition_debug("si_exhibition_cache returning nothing found in cache");
  return NULL;
}
/*
* Manipulate or get information from the cron_cache
*
* If no arguments are supplied, return the entire cron_cache
*
* If a function name is supplied, return the array of arrays of arguments for the function
*
* If function name and arguments are supplied,
* Put information into the cache that cron will use to update the other cached data
*/

function si_exhibition_cron_cache($function_name = FALSE, $arguments = FALSE){
  $current_cron_cache = si_exhibition_cache('cron_check','', FALSE, TRUE);
  if ($current_cron_cache == NULL) $current_cron_cache = array();
  si_exhibition_debug("si_exhibition_cron_cache $function_name");
  if ($function_name === FALSE) return $current_cron_cache;
  if (!array_key_exists($function_name, $current_cron_cache) || !is_array($current_cron_cache[$function_name])){
    $current_cron_cache[$function_name] = array(); 
  }
  if ($arguments === FALSE) return $current_cron_cache[$function_name];

  //Not a simple return, so it's a storage.  Already created the cache if it didn't exist yet
  //Now store the arguments (if they are not stored yet)
  if (!in_array($arguments, $current_cron_cache[$function_name])) $current_cron_cache[$function_name][] = $arguments;
  si_exhibition_cache('cron_check',$function_name, $current_cron_cache[$function_name], TRUE);
}
/**
 * Fetchs some information about the given object's child resources.
 *
 * @param string $pid
 *   The object to query for.
 * @param string $type
 *   The type of resource to look for, can be any one of ('images', 'pdf', 'text', 'csv', 'video').
 * @param string $sort
 *   The sort to apply to the resources label either ('asc', 'desc').
 * @param int $limit
 *   The number of results to limit this query to.  Not used normally since this information is also used for a count of resources.
 * @param int $offset
 *   The offset into the number of results to truncate from the results. Not used normally since this information is also used for a count of resources.
 *
 * @return array
 *   An array of array's containing the following key value pairs for each child concpet of the given object:
 *    pid, label, datastreams
 */
function si_exhibition_get_child_resource_info(FedoraObject $object, $type = 'all', $sort = 'asc', $limit = NULL, $offset = NULL) {
  return si_exhibition_get_child_resource_info_by_id($object->id, $type, $sort, $limit, $offset);
}
function si_exhibition_get_child_resource_info_by_id($object_id, $type = 'all', $sort = 'asc', $limit = NULL, $offset = NULL) {
  //Start cache check for return
  $cache_name = 'child_resource_info';
  $cache_index = $object_id.'_'.$type.'_'.$sort.'_'.$limit.'_'.$offset;
  $to_return = si_exhibition_cache($cache_name,$cache_index);
  if (!empty($to_return)){ return $to_return; }
  si_exhibition_cron_cache(__FUNCTION__, func_get_args());
  //End cache

  $resource_types = array(
    'all' => array(),
    'images' => array('si:imageCModel', 'si:generalImageCModel'),
    'pdf' => array('si:fieldbookCModel'),
    'csv' => array('si:datasetCModel'),
    'text' => FALSE,
    'video' => array('islandora:sp_videoCModel'),
		'audio' => array('islandora:sp-audioCModel')
  );
  $resources = array();
  $resource_types = isset($resource_types[$type]) ? $resource_types[$type] : FALSE;
  if ($resource_types !== FALSE) {
    $repo = tuque_wrapper_get_repository_instance();
    $query = si_exhibition_generate_child_resource_query($object_id, $resource_types, $sort, $limit, $offset);
		$results = $repo->ri->itqlQuery($query);
    $resources = array_map(function($o) { return array('pid' => $o['o']['value'], 'label' => $o['t']['value']); }, $results);
    // @todo replace the checks for each objects existance once we have the appropriate information in the rels to determine if its accessible.
    //$resources =  array_filter($resources, 'si_exhibition_check_object_info_accessible'); //BBB Security changes will necessitate this being used
  }
  $to_return = $resources;
  return si_exhibition_cache($cache_name, $cache_index, $to_return);
}
function si_exhibition_fill_out_resources(&$resources, $show, $limit = NULL, $offset = NULL){
  if (empty($limit)) $limit = sizeof($resources);
  if (empty($offset)) $offset = 0;
  $repo = tuque_wrapper_get_repository_instance();
  si_exhibition_debug('repo from si_exhibition_fill_out_resources');
  foreach ($resources as $key => $resource) {
     if ($key >= $offset && $key <= $offset+$limit){
      $object = $repo->getObject($resource['pid']); // We know it exists from the previous filter call.
      $resources[$key]['models'] = $object->models;
      $resources[$key]['datastreams'] = array();
      try {
        foreach ($object as $dsid => $datatream) {
          $resources[$key]['datastreams'][] = $dsid;
        }
      } catch(Exception $e) { 
        //unset($resources[$key]); 
      } // Ignore when we can't access a resources datastream.
      if (variable_get('si_exhibition_anonymous_only', FALSE)){
        if (!si_exhibition_check_view_permission($resource['pid'])){
          unset($resources[$key]);
        }
      }
    }
  }
}
/**
 * Query for fetching the Child Resources given what type of data they disseminate.
 *
 * @param string $pid
 *   The object to query for.
 * @param string $resource_types
 *   The types of resource to look for, defined as a list of content models, if empty all resources will be returned.
 * @param string $sort
 *   The sort to apply to the resources label either ('asc', 'desc').
 * @param int $limit
 *   The number of results to limit this query to.
 * @param int $offset
 *   The offset into the number of results to truncate from the results.
 *
 * @return string
 *   An itql query string
 */
function si_exhibition_generate_child_resource_query($pid, array $resource_types, $sort = 'asc', $limit = NULL, $offset = NULL) {
  $limit_types = '';
  if (!empty($resource_types)) {
    $limit_type_expression = function($o) {
      return "\$m <mulgara:is> <info:fedora/$o>";
    };
    $limit_types = array_map($limit_type_expression, $resource_types);
    $limit_types = implode(' or ', $limit_types);
    $limit_types = "({$limit_types}) and";
  }
  $limit = isset($limit) ? "limit $limit" : '';
  $offset = isset($offset) ? "offset $offset" : '';
  $to_return = 'select $o $t $m from <#ri> where
    $o <fedora-model:state> <fedora-model:Active> and
    $o <fedora-model:label> $t and
    <info:fedora/' . $pid . '> <fedora-rels-ext:hasResource> $o and
    ' . $limit_types . '
    $o <fedora-model:hasModel> $m
    minus $m <mulgara:is> <info:fedora/si:resourceCModel>
    minus $m <mulgara:is> <info:fedora/fedora-system:FedoraObject-3.0>
    order by $t ' . $sort . '
    ' . $limit . '
    ' . $offset;
  si_exhibition_debug('<pre>'.htmlspecialchars($to_return).'</pre>');
  return $to_return;
}
/*
*/

/**
 * Transforms the given CSV file to an array.
 *
 * @param string $file_path
 *   The path to the file to transform.
 *
 * @return array
 *   An array of array's where each row in the csv file corresponds to a single array of values in the returned array.
 */
function si_exhibition_transform_csv_file_to_array($file_path) {
  /**
   * parseCSV could be an option in the future: http://code.google.com/p/parsecsv-for-php/
   */
  $rows = array();
  if (($file = fopen($file_path, "r")) !== FALSE) {
    while (($row = fgetcsv($file, 100000, ",")) !== FALSE) {
      $rows[] = $row;
    }
    fclose($file);
  }
  return $rows;
}

/**
 * Fetches the get paramters to configure the pager.
 *
 * @return array
 *   array($show, $sort, $limit, $offset)
 */
function si_exhibition_get_pager_parameters() {
  $show = isset($_GET['show']) ? $_GET['show'] : 'all';
  $sort = isset($_GET['sort']) ? $_GET['sort'] : 'asc';
  $limit = isset($_GET['limit']) ? $_GET['limit'] : 10;
  $page =  isset($_GET['page']) ? $_GET['page'] : 0;
  return array($show, $sort, $limit, $page);
}

/**
 * Fetchs the given objects datastream contents.
 * Fails silently if something goes wrong.
 *
 * @param string $pid
 *   The PID of the object.
 * @param string $dsid
 *   The DSID of the datastream to fetch from.
 *
 * @return string
 *   The datastream's contents if successful, an empty string otherwise.
 */
function si_exhibition_get_datastream_contents(FedoraObject $object, $dsid) {
  if (isset($object[$dsid])) {
    $repo = tuque_wrapper_get_repository_instance();
    si_exhibition_debug('repo from si_exhibition_get_datastream_contents');
    return $repo->api->a->getDatastreamDissemination($object->id, $dsid, NULL); // Work around for bug in Tuque API: ISLANDORA-752
  }
  return '';
}

/**
 * Fetchs the given objects CSV datastream contents.
 *
 * @param string $pid
 *   The PID of the object.
 *
 * @return string
 *   The datastream's contents if successful, an empty string otherwise.
 */
function si_exhibition_get_csv_datastream_content(FedoraObject $object) {
  // @todo combined the various csv datastreams into one.
  return si_exhibition_get_datastream_contents($object, 'CSV');
}

/**
 * Fetchs the given objects TEXT datastream contents.
 *
 * @param string $pid
 *   The PID of the object.
 *
 * @return string
 *   The datastream's contents if successful, an empty string otherwise.
 */
function si_exhibition_get_text_datastream_content(FedoraObject $object) {
  return si_exhibition_get_datastream_contents($object, 'TEXT');
}

