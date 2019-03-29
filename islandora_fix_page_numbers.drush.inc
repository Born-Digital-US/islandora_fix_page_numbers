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
        // If the first page is "1", assume the rest of the pages are numbered correctly,
        // and that we don't have to do anything for this issue.
        $first_page = reset($pages);
        if(empty($first_page['page']) || $first_page['page'] !== '1') {
          $batch = ifpn_paged_content_sequence_pages_batch($issue, array_keys($pages));
          batch_set($batch);
          $count++;
          if($count > $limit) {
            break 2;
          }
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

/**
 * Gets the batch definition for re-ordering the given pages.
 *
 * This will order the pages from 1-#pages, the pages will be assigned a
 * sequence number in the order in which they appear.
 *
 * @param AbstractObject $paged_content
 *   The paged_content the pages belong to.
 * @param array $pages
 *   All the pages to sequence.
 *
 * @return array
 *   The batch definition.
 */
function ifpn_paged_content_sequence_pages_batch(AbstractObject $paged_content, array $pages) {
  return array(
    'operations' => array(
      array('ifpn_paged_content_sequence_pages_batch_operation', array(
        $paged_content->id, $pages)),
    ),
    'finished' => 'islandora_paged_content_batch_finished',
    'title' => t("Sequencing @label's pages ...", array('@label' => $paged_content->label)),
    'init_message' => t("Preparing to sequence @label's pages ...", array('@label' => $paged_content->label)),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_paged_content') . '/includes/batch.inc',
  );
}

/**
 * Changes the ordering of the pages.
 *
 * @param AbstractObject $paged_content_id
 *   The paged_content the pages belong to.
 * @param array $pages
 *   All the pages in the order they should be in.
 * @param array $context
 *   The context of this batch operation.
 */
function ifpn_paged_content_sequence_pages_batch_operation($paged_content_id, array $pages, &$context) {
  module_load_include('inc', 'islandora_paged_content', 'includes/utilities');
  module_load_include('inc', 'islandora_paged_content', 'includes/batch');
  $paged_content = islandora_object_load($paged_content_id);
  $page = islandora_paged_content_start_batch_operation($pages, $context);
  if ($page) {
    $page = islandora_object_load($page);
    // Change the label of the page to the sequence number if variable is set.
    if (variable_get('islandora_paged_content_page_label', FALSE)) {
      $page->label = $context['sandbox']['progress'] + 1;
    }

    // Pages are offset from 1 rather than 0.
    islandora_paged_content_set_relationship($page->relationships, ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', $context['sandbox']['progress'] + 1, TRUE);
    islandora_paged_content_set_relationship($page->relationships, ISLANDORA_RELS_EXT_URI, 'isPageNumber', $context['sandbox']['progress'] + 1, TRUE);
    // Assume it worked.
    $context['results']['successful'][] = TRUE;
    if (islandora_paged_content_end_batch_operation($context, 1, array('Sequenced @label', array('@label' => $paged_content->label)))) {
      islandora_paged_content_update_paged_content_thumbnail($paged_content);
    }
    $context['message'] = t('Inserting page "@label" (@pid) at position "@pos"', array(
      '@label' => $page->label,
      '@pid' => $page->id,
      '@pos' => $context['sandbox']['progress']));
  }
}