WITH flow AS (
  SELECT
      date_trunc('minute', dt."timestamp") AS "time",
      SUM(dt."volume")::double precision AS flow_volume
  FROM flow_telemetry dt
  JOIN peripheral p ON p."id" = dt."peripheralId"
  JOIN device d     ON d."id" = p."deviceId"
  WHERE
      $__timeFilter(dt."timestamp")
      AND d."instance" = 'tomatoes'
  GROUP BY 1
),
moisture AS (
  SELECT
      date_trunc('minute', dt."timestamp") AS "time",
      AVG(dt."value")::double precision AS moisture
  FROM numeric_telemetry dt
  JOIN peripheral p ON p."id" = dt."peripheralId"
  JOIN device d     ON d."id" = p."deviceId"
  WHERE
      $__timeFilter(dt."timestamp")
      AND feature = 'moisture'
      AND p."name" LIKE 'soil%'
      AND d."instance" = 'tomatoes'
  GROUP BY 1
),
temperature AS (
  SELECT
      date_trunc('minute', dt."timestamp") AS "time",
      AVG(dt."value")::double precision AS temperature
  FROM numeric_telemetry dt
  JOIN peripheral p ON p."id" = dt."peripheralId"
  JOIN device d     ON d."id" = p."deviceId"
  WHERE
      $__timeFilter(dt."timestamp")
      AND feature = 'temperature'
      AND p."name" LIKE 'soil%'
      AND d."instance" = 'tomatoes'
  GROUP BY 1
)
SELECT
  "time",
  COALESCE(flow.flow_volume, 0) AS volume,
  COALESCE(moisture.moisture, 0) AS moisture,
  COALESCE(temperature.temperature, 0) AS temperature
FROM flow
FULL OUTER JOIN moisture USING ("time")
FULL OUTER JOIN temperature USING ("time")
WHERE
  moisture > 0    -- defend agains incorrect values
  AND moisture <= 100
ORDER BY "time";
