#pragma once

/* Compose public multi-stage results by severity:
 * negative runtime failure > positive capability/partial result > success.
 * Within one severity class the first nonzero result remains authoritative;
 * detailed per-domain diagnostics retain later facts. */
int vbe_result_compose(int accumulated, int stage_result);
