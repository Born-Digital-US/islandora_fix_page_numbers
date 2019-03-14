<?php

function islandora_fix_page_numbers_drush_command() {
  $items = [];
  $items['islandora-fix-page-numbers'] = [
    'callback' => '_islandora_fix_page_numbers',
    'description' => dt('Fix numbering on pages in paged content'),
    'arguments' => [
      'type' => '"n" (newspaper issues) or "b" (books). Required.',
    ],
    'aliases' => ['ifpn'],
    'options' => [
      'pid' => 'Newspaper Issue PID',
      'limit' => "Limit number of books or issues to process."
    ],
    'examples' => [
      'drush ifpn n' => 'Fix page numbering on all newspaper issues on the site.',
      'drush ifpn b --pid=example:1111' => 'Fix page numbering on a particular paged content.',
    ],
  ];
  return $items;
}

function islandora_fix_page_numbers_drush_help($section) {
  switch ($section) {
    case 'drush:islandora-fix-page-numbers':
      return dt("Renumbers book or newspaper pages.");
  }
}

function _islandora_fix_page_numbers($type) {
  $pid = drush_get_option('pid', FALSE);
  $limit = max(1, drush_get_option('limit', 999999));
  $count = 0;
  $queries = [];
  if ($pid) {
    $queries[] = 'PID:"' . $pid . '"';
  }
  else {
    $queries[] = 'RELS_EXT_hasModel_uri_s:"info:fedora/islandora:' . ($type == 'b' ? 'bookCModel' : 'newspaperIssueCModel') . '"';
  }
  $queries = implode(' AND ', $queries);
  module_load_include('inc', 'islandora_paged_content', 'includes/manage.pages');
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  module_load_include('inc', 'islandora_paged_content', 'includes/batch');
  $query_processor = new IslandoraSolrQueryProcessor();
  $query_processor->solrQuery = $queries;
  $query_processor->solrParams['fl'] = 'PID';
  $page = 0;
  $query_processor->solrLimit = '20';
  do {
    $query_processor->solrStart = max(0, $page) * $query_processor->solrLimit;
    $query_processor->executeQuery(FALSE);
    $results = $query_processor->islandoraSolrResult;
    if (!empty($results['response']['objects'])) {
      foreach ($results['response']['objects'] as $result) {
        $issue = islandora_object_load($result['PID']);
        $pages = islandora_paged_content_get_pages($issue);
        $batch = islandora_paged_content_sequence_pages_batch($issue, array_keys($pages));
        batch_set($batch);
        $count++;
        if($count > $limit) {
          break 2;
        }
      }
    }
    $page++;

  } while (count($results['response']['objects']) > 0 && $count <= $limit);

  if(!empty($batch)) {
    //Get the batch process all ready!
    $batch =& batch_get();

    //Because we are doing this on the back-end, we set progressive to false.
    $batch['progressive'] = FALSE;

    //Start processing the batch operations.
    drush_backend_batch_process();
  }
  else {
    drush_log(dt("No batch created!"));
  }

}